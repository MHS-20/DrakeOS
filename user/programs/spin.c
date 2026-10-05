/* Busy loop that never yields: the timer interrupt still preempts it. */
#include <drake.h>

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? atoi(argv[1]) : 3;
    for (int r = 1; r <= rounds; r++) {
        for (volatile unsigned i = 0; i < 50000000u; i++)
            ;
        printf("[spin %d] round %d done\n", getpid(), r);
    }
    return 0;
}
