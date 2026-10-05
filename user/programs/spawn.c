/* A user program starting other programs with exec + wait. */
#include <drake.h>

int main(void)
{
    char *argv[] = { "hello", "from", "spawn", 0 };
    int pid = exec("/hello", argv);
    if (pid < 0) {
        puts("spawn: exec failed");
        return 1;
    }
    int status = wait(pid);
    printf("spawn: child %d exited with %d\n", pid, status);
    return 0;
}
