/* The DrakeOS shell: a kernel thread with line editing, history and the built-in commands. */
#include <arch.h>
#include <console.h>
#include <cpu.h>
#include <drivers.h>
#include <fs.h>
#include <keyboard.h>
#include <mm.h>
#include <proc.h>
#include <shell.h>
#include <string.h>
#include <vga.h>

#define LINE_MAX    160
#define MAX_ARGS    16
#define HISTORY     16

static char history[HISTORY][LINE_MAX];
static int history_count;

/* ------------------------------------------------------------------ line editing */

static void erase_line(int len)
{
    while (len--)
        console_write("\b", 1);
}

static int readline(char *buf, int size)
{
    int len = 0, browse = history_count;
    for (;;) {
        int c = kbd_getchar();
        if (c == '\n') {
            console_putc('\n');
            buf[len] = '\0';
            return len;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                console_write("\b", 1);
            }
        } else if (c == 3) {                   /* Ctrl+C: drop the line */
            console_write("^C\n", 3);
            buf[0] = '\0';
            return -1;
        } else if (c == 12) {                  /* Ctrl+L: clear the screen */
            vga_clear();
            buf[len] = '\0';
            return -2;
        } else if (c == KEY_UP || c == KEY_DOWN) {
            int target = browse + (c == KEY_UP ? -1 : 1);
            if (target < 0 || target > history_count || target < history_count - HISTORY)
                continue;
            browse = target;
            erase_line(len);
            if (browse == history_count)
                buf[0] = '\0';
            else
                strlcpy(buf, history[browse % HISTORY], size);
            len = strlen(buf);
            console_write(buf, len);
        } else if (c >= ' ' && c < 0x7F && len < size - 1) {
            buf[len++] = (char)c;
            console_putc((char)c);
        }
    }
}

static void remember(const char *line)
{
    if (history_count && !strcmp(history[(history_count - 1) % HISTORY], line))
        return;
    strlcpy(history[history_count % HISTORY], line, LINE_MAX);
    history_count++;
}

static int tokenize(char *line, char *argv[], int max)
{
    int argc = 0;
    while (*line && argc < max) {
        while (*line == ' ' || *line == '\t')
            *line++ = '\0';
        if (!*line)
            break;
        if (*line == '"') {                    /* "quoted argument" */
            argv[argc++] = ++line;
            while (*line && *line != '"')
                line++;
            if (*line)
                *line++ = '\0';
            continue;
        }
        argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\t')
            line++;
    }
    return argc;
}

/* "foo" -> "/foo", "disk/foo" -> "/disk/foo"; absolute paths are kept. */
void shell_resolve(const char *name, char *out, int size)
{
    if (name[0] == '/')
        strlcpy(out, name, size);
    else
        snprintf(out, size, "/%s", name);
}

static void prompt(void)
{
    uint8_t attr = vga_get_attr();
    vga_set_attr(VGA_ATTR(VGA_LIGHT_RED, attr >> 4));
    kprintf("drake");
    vga_set_attr(attr);
    kprintf("> ");
}

static void banner(void)
{
    uint8_t attr = vga_get_attr();
    vga_set_attr(VGA_ATTR(VGA_LIGHT_RED, attr >> 4));
    kprintf("  ____            _         ___  ____\n"
            " |  _ \\ _ __ __ _| | _____ / _ \\/ ___|\n"
            " | | | | '__/ _` | |/ / _ \\ | | \\___ \\\n"
            " | |_| | | | (_| |   <  __/ |_| |___) |\n"
            " |____/|_|  \\__,_|_|\\_\\___|\\___/|____/\n\n");
    vga_set_attr(attr);
    kprintf("Type 'help' for the list of commands.\n\n");
}

int shell_main(void *arg)
{
    (void)arg;
    char line[LINE_MAX], copy[LINE_MAX];
    char *argv[MAX_ARGS + 1];
    banner();
    for (;;) {
        prompt();
        int len = readline(line, sizeof line);
        if (len <= 0)
            continue;
        remember(line);
        strlcpy(copy, line, sizeof copy);
        int argc = tokenize(copy, argv, MAX_ARGS);
        if (!argc)
            continue;
        argv[argc] = NULL;
        const struct command *cmd = shell_find_command(argv[0]);
        if (cmd)
            cmd->fn(argc, argv);
        else
            kprintf("%s: unknown command (try 'help')\n", argv[0]);
    }
}
