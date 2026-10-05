/* Built-in shell commands. */
#include <arch.h>
#include <console.h>
#include <cpu.h>
#include <drivers.h>
#include <fs.h>
#include <keyboard.h>
#include <layout.h>
#include <mm.h>
#include <proc.h>
#include <shell.h>
#include <string.h>
#include <vga.h>

#define PATH_LEN 96

extern const uint8_t logo_data[];            /* logo_blob.asm: palette + 200x200 pixels */
#define LOGO_SIZE 200

static void cmd_help(int argc, char **argv);

/* ------------------------------------------------------------------ console and system */

static void cmd_clear(int argc, char **argv)
{
    (void)argc, (void)argv;
    vga_clear();
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        kprintf("%s%s", argv[i], i + 1 < argc ? " " : "");
    kprintf("\n");
}

static void cmd_color(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("colors: 0 black 1 blue 2 green 3 cyan 4 red 5 magenta 6 brown 7 grey\n"
                "        8-15 bright versions (e.g. 'color 14 1' = yellow on blue)\n");
        return;
    }
    int fg = atoi(argv[1]) & 15;
    int bg = argc > 2 ? atoi(argv[2]) & 7 : vga_get_attr() >> 4;
    vga_recolor(VGA_ATTR(fg, bg));
}

static void cmd_uptime(int argc, char **argv)
{
    (void)argc, (void)argv;
    uint32_t t = timer_ticks(), s = t / TIMER_HZ;
    kprintf("up %u:%02u:%02u (%u ticks at %u Hz)\n", s / 3600, s / 60 % 60, s % 60, t, TIMER_HZ);
}

static void cmd_mem(int argc, char **argv)
{
    (void)argc, (void)argv;
    uint32_t total = pmm_total_frames(), free = pmm_free_frames();
    kprintf("physical: %u KB total, %u KB used, %u KB free (4 KB frames)\n",
            total * 4, (total - free) * 4, free * 4);
    kprintf("kernel heap: %u bytes in use\n", (uint32_t)heap_used());
}

static void cmd_ps(int argc, char **argv)
{
    (void)argc, (void)argv;
    struct proc_info list[MAX_PROCS];
    int n = process_list(list, MAX_PROCS);
    kprintf("  PID  STATE     MODE    NAME\n");
    for (int i = 0; i < n; i++)
        kprintf("%5d  %-8s  %-6s  %s\n", list[i].pid, proc_state_name(list[i].state),
                list[i].is_user ? "user" : "kernel", list[i].name);
}

static void cmd_kill(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("usage: kill PID\n");
        return;
    }
    if (process_kill(atoi(argv[1])) < 0)
        kprintf("kill: no such user process\n");
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("Rebooting...\n");
    cli();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
    outb(0x64, 0xFE);                         /* pulse the CPU reset line via the 8042 */
    halt_forever();
}

static void cmd_shutdown(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("Shutting down.\n");
    outw(0x604, 0x2000);                      /* QEMU ACPI power-off (pc/q35 machines) */
    outw(0xB004, 0x2000);                     /* older QEMU/Bochs */
    kprintf("Power-off failed; it is now safe to turn off the computer.\n");
    halt_forever();
}

static void cmd_halt(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("CPU halted.\n");
    halt_forever();
}

/* ------------------------------------------------------------------ files */

static void cmd_ls(int argc, char **argv)
{
    char path[PATH_LEN];
    if (argc > 1)
        shell_resolve(argv[1], path, sizeof path);
    else
        strlcpy(path, "/", sizeof path);
    struct dirent de;
    int i = 0;
    for (; vfs_readdir(path, i, &de) == 0; i++) {
        if (de.type == VFS_DIR)
            kprintf("  %-24s <dir>\n", de.name);
        else if (de.type == VFS_DEVICE)
            kprintf("  %-24s <device>\n", de.name);
        else
            kprintf("  %-24s %u bytes\n", de.name, de.size);
    }
    if (!i && !strcmp(path, "/disk") && !drakefs_is_mounted())
        kprintf("ls: no DrakeFS disk mounted (use 'format' on a blank data disk)\n");
    else if (!i && strcmp(path, "/disk"))
        kprintf("ls: %s: not a directory or empty\n", path);
}

static void cmd_cat(int argc, char **argv)
{
    char path[PATH_LEN], buf[256];
    for (int a = 1; a < argc; a++) {
        shell_resolve(argv[a], path, sizeof path);
        struct file *f = vfs_open(path, O_READ);
        if (!f) {
            kprintf("cat: %s: not found\n", path);
            continue;
        }
        int n;
        while ((n = vfs_read(f, buf, sizeof buf)) > 0)
            console_write(buf, n);
        vfs_close(f);
    }
}

static void cmd_write(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("usage: write FILE [TEXT...]   (writes /disk/FILE)\n");
        return;
    }
    char path[PATH_LEN];
    if (!strncmp(argv[1], "/disk/", 6) || !strncmp(argv[1], "disk/", 5))
        shell_resolve(argv[1], path, sizeof path);
    else
        snprintf(path, sizeof path, "/disk/%s", argv[1]);
    struct file *f = vfs_open(path, O_WRITE | O_CREATE | O_TRUNC);
    if (!f) {
        kprintf("write: cannot create %s (is the disk formatted?)\n", path);
        return;
    }
    for (int i = 2; i < argc; i++) {
        vfs_write(f, argv[i], strlen(argv[i]));
        vfs_write(f, i + 1 < argc ? " " : "\n", 1);
    }
    vfs_close(f);
}

static void cmd_rm(int argc, char **argv)
{
    char path[PATH_LEN];
    for (int i = 1; i < argc; i++) {
        shell_resolve(argv[i], path, sizeof path);
        if (vfs_unlink(path) < 0)
            kprintf("rm: %s: cannot remove (only /disk files can be removed)\n", path);
    }
}

static void cmd_format(int argc, char **argv)
{
    (void)argc, (void)argv;
    if (!ata_sector_count()) {
        kprintf("format: no data disk attached (primary slave)\n");
        return;
    }
    if (drakefs_format() < 0)
        kprintf("format: failed\n");
    else
        kprintf("format: DrakeFS created, %u KB free\n", drakefs_free_sectors() / 2);
}

static void cmd_df(int argc, char **argv)
{
    (void)argc, (void)argv;
    if (!drakefs_is_mounted())
        kprintf("df: no DrakeFS disk mounted\n");
    else
        kprintf("/disk: %u KB total, %u KB free\n", ata_sector_count() / 2, drakefs_free_sectors() / 2);
}

/* ------------------------------------------------------------------ programs */

static void cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        kprintf("usage: run PROGRAM [ARGS...] [&]\n");
        return;
    }
    int background = !strcmp(argv[argc - 1], "&");
    if (background)
        argv[--argc] = NULL;
    char path[PATH_LEN];
    shell_resolve(argv[1], path, sizeof path);
    int pid = process_spawn(path, argc - 1, argv + 1, background);
    if (pid < 0) {
        kprintf("run: %s: cannot execute\n", path);
        return;
    }
    if (background) {
        kprintf("[%d] started\n", pid);
        return;
    }
    int status = process_wait(pid);
    if (status)
        kprintf("[%d] exited with status %d\n", pid, status);
}

/* ------------------------------------------------------------------ sound */

static void cmd_beep(int argc, char **argv)
{
    uint32_t hz = argc > 1 ? (uint32_t)atoi(argv[1]) : 880;
    uint32_t ms = argc > 2 ? (uint32_t)atoi(argv[2]) : 200;
    speaker_beep(hz, ms);
}

static void cmd_play(int argc, char **argv)
{
    (void)argc, (void)argv;
    /* "Ode to Joy", first phrase: frequencies in Hz, 0 = rest */
    static const uint16_t notes[] = { 330, 330, 349, 392, 392, 349, 330, 294, 262, 262, 294, 330,
                                      330, 294, 294, 0 };
    for (unsigned i = 0; i < sizeof notes / sizeof *notes; i++) {
        speaker_beep(notes[i], i == 12 ? 450 : i == 14 ? 450 : 280);
        process_sleep(30);
    }
}

/* ------------------------------------------------------------------ graphics */

static void wait_key_in_graphics(void)
{
    while (kbd_trygetchar() >= 0)             /* drop keys typed before */
        ;
    kbd_getchar();
}

static void cmd_logo(int argc, char **argv)
{
    (void)argc, (void)argv;
    gfx_enter();
    for (int i = 0; i < 256; i++)
        gfx_set_palette(i, logo_data[i * 3], logo_data[i * 3 + 1], logo_data[i * 3 + 2]);
    int darkest = 0;                          /* letterbox in the logo's darkest colour */
    for (int i = 1; i < 256; i++)
        if (logo_data[i * 3] + logo_data[i * 3 + 1] + logo_data[i * 3 + 2] <
            logo_data[darkest * 3] + logo_data[darkest * 3 + 1] + logo_data[darkest * 3 + 2])
            darkest = i;
    gfx_fill_rect(0, 0, GFX_WIDTH, GFX_HEIGHT, darkest);
    const uint8_t *pixels = logo_data + 768;
    int ox = (GFX_WIDTH - LOGO_SIZE) / 2;
    for (int y = 0; y < LOGO_SIZE; y++)
        for (int x = 0; x < LOGO_SIZE; x++)
            gfx_put_pixel(ox + x, y, pixels[y * LOGO_SIZE + x]);
    gfx_present();
    wait_key_in_graphics();
    gfx_leave();
}

static void cmd_gfx(int argc, char **argv)
{
    (void)argc, (void)argv;
    gfx_enter();
    for (int y = 0; y < GFX_HEIGHT; y++)                 /* background: grey ramp */
        gfx_fill_rect(0, y, GFX_WIDTH, 1, 232 + y * 23 / GFX_HEIGHT);
    for (int i = 0; i < 216; i++)                         /* the 6x6x6 colour cube */
        gfx_fill_rect(16 + (i % 36) * 8, 40 + (i / 36) * 12, 7, 11, 16 + i);
    for (int i = 0; i < 16; i++)                          /* text-mode colours */
        gfx_fill_rect(16 + i * 18, 120, 16, 16, i);
    for (int i = 0; i < 12; i++)                          /* line fan */
        gfx_draw_line(160, 199, i * 29, 145, 16 + i * 18);
    gfx_draw_text(16, 8, "DrakeOS mode 13h - 320x200, 256 colours", 15);
    gfx_draw_text(16, 24, "press any key to return", 14);
    gfx_present();
    wait_key_in_graphics();
    gfx_leave();
}

static void draw_cursor(int x, int y, uint8_t color)
{
    gfx_draw_line(x - 3, y, x + 3, y, color);
    gfx_draw_line(x, y - 3, x, y + 3, color);
}

static void canvas_dot(uint8_t *canvas, int x, int y, uint8_t color)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (x + dx >= 0 && x + dx < GFX_WIDTH && y + dy >= 12 && y + dy < GFX_HEIGHT)
                canvas[(y + dy) * GFX_WIDTH + x + dx] = color;
}

/* A 3-pixel-wide segment (Bresenham), so fast mouse moves still give a continuous stroke. */
static void canvas_stroke(uint8_t *canvas, int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        canvas_dot(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
            err += dy, x0 += sx;
        if (e2 <= dx)
            err += dx, y0 += sy;
    }
}

static void cmd_paint(int argc, char **argv)
{
    (void)argc, (void)argv;
    static uint8_t canvas[GFX_WIDTH * GFX_HEIGHT];
    static const uint8_t palette[] = { 15, 12, 10, 9, 14, 13, 11, 0 };
    uint8_t color = 15;
    memset(canvas, 0, sizeof canvas);
    mouse_set_bounds(GFX_WIDTH, GFX_HEIGHT);
    gfx_enter();
    struct mouse_state last = mouse_get();
    for (;;) {
        int key = kbd_trygetchar();
        if (key == 27 || key == 'q')
            break;
        if (key == 'c')
            memset(canvas, 0, sizeof canvas);
        struct mouse_state m = mouse_get();
        if (m.buttons & 1)                     /* left button: draw, joining the samples */
            canvas_stroke(canvas, (last.buttons & 1) ? last.x : m.x, (last.buttons & 1) ? last.y : m.y,
                          m.x, m.y, color);
        if ((m.buttons & 2) && !(last.buttons & 2))   /* right click: next colour */
            for (unsigned i = 0; i < sizeof palette; i++)
                if (palette[i] == color) {
                    color = palette[(i + 1) % sizeof palette];
                    break;
                }
        last = m;
        for (int y = 0; y < GFX_HEIGHT; y++)
            for (int x = 0; x < GFX_WIDTH; x++)
                gfx_put_pixel(x, y, canvas[y * GFX_WIDTH + x]);
        gfx_fill_rect(0, 0, GFX_WIDTH, 12, 8);
        gfx_fill_rect(2, 2, 8, 8, color);
        gfx_draw_text(14, -2, "L:draw R:colour c:clear Esc:quit", 15);
        draw_cursor(m.x, m.y, color == 15 ? 12 : 15);
        gfx_wait_vsync();
        gfx_present();
        process_sleep(15);
    }
    gfx_leave();
}

/* Text-mode animation: rewrites every cell until a key is pressed. */
static void cmd_vid(int argc, char **argv)
{
    (void)argc, (void)argv;
    uint8_t *cells = P2V(0xB8000);
    uint8_t c = 'A';
    while (kbd_trygetchar() < 0) {
        for (int i = 0; i < VGA_COLS * VGA_ROWS * 2; i++)
            cells[i] = c++;
        process_sleep(40);
    }
    vga_clear();
}

static void cmd_hi(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("HELLO, HAVE A GOOD JOURNEY LEARNING\n");
}

static void cmd_about(int argc, char **argv)
{
    (void)argc, (void)argv;
    kprintf("DrakeOS - a 32-bit x86 teaching kernel.\n"
            "Higher-half paging, preemptive multitasking, ring-3 processes with int 0x80\n"
            "system calls, ELF loading, initrd + DrakeFS, VGA text/graphics, PS/2 keyboard\n"
            "and mouse, PC speaker, ATA PIO. Boots from its own BIOS loader or from GRUB.\n");
}

static const struct command commands[] = {
    { "help",     "[COMMAND]",       "list commands or describe one", cmd_help },
    { "clear",    "",                "clear the screen (also Ctrl+L)", cmd_clear },
    { "echo",     "TEXT...",         "print the arguments", cmd_echo },
    { "color",    "FG [BG]",         "change the console colours (0-15)", cmd_color },
    { "uptime",   "",                "time since boot", cmd_uptime },
    { "ticks",    "",                "same as uptime", cmd_uptime },
    { "mem",      "",                "physical memory and heap usage", cmd_mem },
    { "ps",       "",                "list processes", cmd_ps },
    { "kill",     "PID",             "terminate a user process", cmd_kill },
    { "ls",       "[DIR]",           "list /, /disk or /dev", cmd_ls },
    { "cat",      "FILE...",         "print files (/name = initrd, /disk/name = disk)", cmd_cat },
    { "write",    "FILE TEXT...",    "write TEXT to /disk/FILE", cmd_write },
    { "rm",       "FILE...",         "delete /disk files", cmd_rm },
    { "format",   "",                "create an empty DrakeFS on the data disk", cmd_format },
    { "df",       "",                "data disk usage", cmd_df },
    { "run",      "PROG [ARGS] [&]", "run a user program (& = in the background)", cmd_run },
    { "beep",     "[HZ] [MS]",       "sound the PC speaker", cmd_beep },
    { "play",     "",                "play a short tune", cmd_play },
    { "gfx",      "",                "VGA mode 13h demo", cmd_gfx },
    { "logo",     "",                "show the DrakeOS logo", cmd_logo },
    { "paint",    "",                "draw with the PS/2 mouse", cmd_paint },
    { "vid",      "",                "text-mode animation (any key stops it)", cmd_vid },
    { "hi",       "",                "a greeting", cmd_hi },
    { "about",    "",                "what DrakeOS is", cmd_about },
    { "reboot",   "",                "reset the machine", cmd_reboot },
    { "shutdown", "",                "power off (QEMU)", cmd_shutdown },
    { "halt",     "",                "stop the CPU", cmd_halt },
};

#define NCOMMANDS (sizeof commands / sizeof *commands)

const struct command *shell_find_command(const char *name)
{
    for (unsigned i = 0; i < NCOMMANDS; i++)
        if (!strcmp(commands[i].name, name))
            return &commands[i];
    return NULL;
}

static void cmd_help(int argc, char **argv)
{
    if (argc > 1) {
        const struct command *c = shell_find_command(argv[1]);
        if (c)
            kprintf("%s %s\n  %s\n", c->name, c->usage, c->help);
        else
            kprintf("help: no command '%s'\n", argv[1]);
        return;
    }
    for (unsigned i = 0; i < NCOMMANDS; i++)      /* two columns so the list fits on screen */
        kprintf("%-9s %-29.29s%s", commands[i].name, commands[i].help, i % 2 ? "\n" : " ");
    if (NCOMMANDS % 2)
        kprintf("\n");
    kprintf("'help COMMAND' shows the full description and usage.\n");
    kprintf("User programs live in / (try 'ls' then 'run hello').\n");
}
