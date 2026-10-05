/* cat through system calls: open, read, write, close. */
#include <drake.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        puts("usage: ucat PATH...");
        return 1;
    }
    char buf[128];
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_READ);
        if (fd < 0) {
            printf("ucat: %s: cannot open\n", argv[i]);
            continue;
        }
        int n;
        while ((n = read(fd, buf, sizeof buf)) > 0)
            write(STDOUT, buf, n);
        close(fd);
    }
    return 0;
}
