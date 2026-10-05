/* kmain: brings up every subsystem in dependency order, then starts the shell. */
#include <console.h>
#include <layout.h>
#include <multiboot.h>
#include <serial.h>
#include <vga.h>

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    serial_init();
    vga_init();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not started by a Multiboot loader (eax=%x)", magic);
    multiboot_info_t *mbi = P2V(mbi_phys);

    vga_set_attr(VGA_ATTR(VGA_LIGHT_RED, VGA_BLACK));
    kprintf("DrakeOS\n");
    vga_set_attr(VGA_ATTR(VGA_LIGHT_GREY, VGA_BLACK));
    if (mbi->flags & MB_INFO_LOADER_NAME)
        kprintf("Booted by %s\n", (const char *)P2V(mbi->boot_loader_name));
    if (mbi->flags & MB_INFO_MEMORY)
        kprintf("Memory: %u KB low, %u KB high\n", mbi->mem_lower, mbi->mem_upper);
    for (;;)
        asm volatile("hlt");
}
