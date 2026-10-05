/* The first user program: prints its arguments from ring 3 through the write system call. */
#include <drake.h>

int main(int argc, char **argv)
{
    printf("Hello from user mode! I am pid %d.\n", getpid());
    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i]);
    return 0;
}
