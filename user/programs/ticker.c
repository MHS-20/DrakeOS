/* Prints a counter with sleeps in between: run two in the background to watch preemption. */
#include <drake.h>

int main(int argc, char **argv)
{
    int count = argc > 1 ? atoi(argv[1]) : 5;
    int pid = getpid();
    for (int i = 1; i <= count; i++) {
        printf("[ticker %d] %d/%d\n", pid, i, count);
        sleep_ms(500);
    }
    return 0;
}
