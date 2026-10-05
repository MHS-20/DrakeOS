/* System call numbers: int 0x80, number in eax, arguments in ebx, ecx, edx, result in eax. */
#ifndef DRAKE_SYSCALL_H
#define DRAKE_SYSCALL_H

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_YIELD  3
#define SYS_GETPID 4
#define SYS_SLEEP  5
#define SYS_OPEN   6
#define SYS_CLOSE  7
#define SYS_EXEC   8
#define SYS_WAIT   9

void syscall_init(void);

#endif
