/* Reads a line from the keyboard through the console device. */
#include <drake.h>

int main(void)
{
    char name[64];
    printf("What is your name? ");
    int n = read(STDIN, name, sizeof name - 1);
    if (n <= 0)
        return 1;
    if (name[n - 1] == '\n')
        n--;
    name[n] = '\0';
    printf("Welcome to DrakeOS, %s!\n", name);
    return 0;
}
