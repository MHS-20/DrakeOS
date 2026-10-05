/* Shell entry point and the command table. */
#ifndef DRAKE_SHELL_H
#define DRAKE_SHELL_H

struct command {
    const char *name;
    const char *usage;
    const char *help;
    void (*fn)(int argc, char **argv);
};

int shell_main(void *arg);
const struct command *shell_find_command(const char *name);
void shell_resolve(const char *name, char *out, int size);

#endif
