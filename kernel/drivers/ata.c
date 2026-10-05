/*
 * ATA PIO driver for the primary-bus slave drive (the DrakeFS data disk), 28-bit LBA, polling.
 * The primary master is the boot disk and is never written.
 */
#include <arch.h>
#include <console.h>
#include <cpu.h>
#include <drivers.h>

#define ATA_DATA      0x1F0
#define ATA_ERROR     0x1F1
#define ATA_COUNT     0x1F2
#define ATA_LBA0      0x1F3
#define ATA_LBA1      0x1F4
#define ATA_LBA2      0x1F5
#define ATA_DRIVE     0x1F6
#define ATA_STATUS    0x1F7
#define ATA_COMMAND   0x1F7
#define ATA_CONTROL   0x3F6

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_BSY  0x80

#define CMD_READ   0x20
#define CMD_WRITE  0x30
#define CMD_FLUSH  0xE7
#define CMD_IDENTIFY 0xEC

#define DRIVE_SLAVE 0xF0              /* LBA mode | bits 5,7 set | slave */

static uint32_t sectors;

static void delay400ns(void)
{
    for (int i = 0; i < 4; i++)
        inb(ATA_CONTROL);             /* alternate status: reading it has no side effects */
}

static int wait_not_busy(void)
{
    for (int i = 0; i < 1000000; i++)
        if (!(inb(ATA_STATUS) & ST_BSY))
            return 0;
    return -1;
}

/* Waits for DRQ; fails on ERR/DF or timeout. */
static int wait_drq(void)
{
    for (int i = 0; i < 1000000; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ST_BSY)
            continue;
        if (st & (ST_ERR | ST_DF))
            return -1;
        if (st & ST_DRQ)
            return 0;
    }
    return -1;
}

static void select_lba(uint32_t lba, uint8_t count)
{
    outb(ATA_DRIVE, DRIVE_SLAVE | ((lba >> 24) & 0x0F));
    delay400ns();
    outb(ATA_COUNT, count);
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_LBA2, (lba >> 16) & 0xFF);
}

int ata_init(void)
{
    outb(ATA_CONTROL, 0x02);          /* nIEN: we poll, so no IRQ 14 */
    outb(ATA_DRIVE, 0xB0);            /* select the slave */
    delay400ns();
    outb(ATA_COUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, CMD_IDENTIFY);
    if (inb(ATA_STATUS) == 0) {
        klog(LOG_INFO, "ata: no primary slave drive");
        return -1;
    }
    if (wait_not_busy() < 0 || inb(ATA_LBA1) || inb(ATA_LBA2)) {   /* non-zero: ATAPI/SATA */
        klog(LOG_WARN, "ata: primary slave is not an ATA disk");
        return -1;
    }
    if (wait_drq() < 0)
        return -1;
    uint16_t id[256];
    insw(ATA_DATA, id, 256);
    sectors = id[60] | (uint32_t)id[61] << 16;   /* words 60-61: LBA28 sector count */
    klog(LOG_INFO, "ata: data disk with %u sectors (%u KB)", sectors, sectors / 2);
    return sectors ? 0 : -1;
}

uint32_t ata_sector_count(void) { return sectors; }

int ata_read(uint32_t lba, uint32_t count, void *buf)
{
    if (!sectors || lba + count > sectors)
        return -1;
    uint16_t *p = buf;
    uint32_t flags = irq_save();
    int rc = 0;
    while (count && !rc) {
        uint8_t n = count > 255 ? 255 : (uint8_t)count;
        if (wait_not_busy() < 0) {
            rc = -1;
            break;
        }
        select_lba(lba, n);
        outb(ATA_COMMAND, CMD_READ);
        for (int s = 0; s < n; s++, p += 256) {
            delay400ns();
            if (wait_drq() < 0) {
                rc = -1;
                break;
            }
            insw(ATA_DATA, p, 256);
        }
        lba += n;
        count -= n;
    }
    irq_restore(flags);
    return rc;
}

int ata_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (!sectors || lba + count > sectors)
        return -1;
    const uint16_t *p = buf;
    uint32_t flags = irq_save();
    int rc = 0;
    while (count && !rc) {
        uint8_t n = count > 255 ? 255 : (uint8_t)count;
        if (wait_not_busy() < 0) {
            rc = -1;
            break;
        }
        select_lba(lba, n);
        outb(ATA_COMMAND, CMD_WRITE);
        for (int s = 0; s < n; s++, p += 256) {
            delay400ns();
            if (wait_drq() < 0) {
                rc = -1;
                break;
            }
            outsw(ATA_DATA, p, 256);
        }
        lba += n;
        count -= n;
    }
    if (!rc) {
        outb(ATA_COMMAND, CMD_FLUSH);    /* make sure the data leaves the drive's cache */
        rc = wait_not_busy();
    }
    irq_restore(flags);
    return rc;
}
