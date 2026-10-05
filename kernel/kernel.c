/* kmain: brings up every subsystem in dependency order, then becomes the idle task. */
#include <arch.h>
#include <console.h>
#include <cpu.h>
#include <drivers.h>
#include <fs.h>
#include <keyboard.h>
#include <layout.h>
#include <mm.h>
#include <multiboot.h>
#include <proc.h>
#include <serial.h>
#include <shell.h>
#include <syscall.h>
#include <vga.h>

static void step(const char *what)
{
    kprintf("  [ ok ] %s\n", what);
}

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    serial_init();
    vga_init();
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not started by a Multiboot loader (eax=%x)", magic);
    const multiboot_info_t *mbi = P2V(mbi_phys);

    vga_set_attr(VGA_ATTR(VGA_LIGHT_RED, VGA_BLACK));
    kprintf("DrakeOS is booting");
    vga_set_attr(VGA_ATTR(VGA_LIGHT_GREY, VGA_BLACK));
    if (mbi->flags & MB_INFO_LOADER_NAME)
        kprintf(" (loaded by %s)", (const char *)P2V(mbi->boot_loader_name));
    kprintf("\n");

    gdt_init();
    step("GDT and TSS");
    idt_init();
    pic_init();
    step("IDT, exceptions, PIC remapped to vectors 32-47");
    pmm_init(mbi);
    vmm_init();
    heap_init();
    kprintf("  [ ok ] memory: %u KB free, higher-half paging, kernel heap\n", pmm_free_frames() * 4);
    gfx_init();                              /* must run while the screen is still in text mode */
    vfs_init();
    step("VFS with initrd");
    sched_init();
    syscall_init();
    keyboard_init();
    if (mouse_init() == 0)
        step("PS/2 keyboard and mouse");
    else
        step("PS/2 keyboard (no mouse)");
    pit_init(TIMER_HZ);
    step("PIT at 100 Hz, preemptive scheduler");
    if (ata_init() == 0) {
        if (drakefs_mount() == 0)
            kprintf("  [ ok ] data disk: DrakeFS mounted on /disk\n");
        else
            kprintf("  [warn] data disk is not formatted: run 'format'\n");
    } else {
        kprintf("  [warn] no data disk (attach one as primary slave for /disk)\n");
    }

    if (kthread_create("shell", shell_main, NULL) < 0)
        panic("cannot start the shell");
    klog(LOG_INFO, "boot complete");
    kprintf("\n");

    sti();
    for (;;) {                                /* idle task (pid 0) */
        sched_yield();
        asm volatile("sti; hlt");
    }
}
