/* Shows memory protection: user code may not touch kernel memory or run privileged instructions. */
#include <drake.h>

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "kernel";
    if (!strcmp(what, "cli")) {
        puts("fault: executing cli in ring 3 (expect a general protection fault)");
        asm volatile("cli");
    } else if (!strcmp(what, "null")) {
        puts("fault: reading address 0 (expect a page fault)");
        volatile int *p = 0;
        printf("%d\n", *p);
    } else {
        puts("fault: writing to kernel memory at 0xC0100000 (expect a page fault)");
        *(volatile int *)0xC0100000 = 42;
    }
    puts("fault: still alive - protection failed!");
    return 1;
}
