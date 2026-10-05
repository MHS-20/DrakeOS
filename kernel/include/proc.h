/* Processes, kernel threads, the round-robin preemptive scheduler and blocking primitives. */
#ifndef DRAKE_PROC_H
#define DRAKE_PROC_H

#include <stdint.h>

#define MAX_PROCS     32
#define MAX_FDS       8
#define KSTACK_SIZE   16384
#define TIME_SLICE    2              /* timer ticks per quantum (20 ms at 100 Hz) */

struct file;
struct registers;

enum proc_state { P_UNUSED, P_READY, P_RUNNING, P_BLOCKED, P_SLEEPING, P_ZOMBIE };

struct process {
    int pid;
    enum proc_state state;
    char name[16];
    int is_user;
    int detached;                    /* nobody will wait(): reaped automatically */
    int killed;
    int exit_status;
    uint32_t esp;                    /* saved kernel stack pointer while switched out */
    void *kstack;                    /* base of the kernel stack allocation */
    uint32_t kstack_top;
    uint32_t pd;                     /* physical address of the page directory */
    uint32_t wake_tick;
    const void *wait_channel;
    struct process *parent;
    struct file *fds[MAX_FDS];
};

void sched_init(void);               /* turns the boot context into process 0 (idle) */
void sched_tick(void);               /* called by the timer interrupt */
void schedule(void);                 /* call with interrupts disabled */
void sched_yield(void);
struct process *current_process(void);

int kthread_create(const char *name, int (*fn)(void *), void *arg);
int process_spawn(const char *path, int argc, char *const argv[], int detached);
int process_wait(int pid);
__attribute__((noreturn)) void process_exit(int status);
int process_kill(int pid);
void process_kill_current(uint32_t reason);
void process_check_signals(struct registers *r);
void process_sleep(uint32_t ms);

/* Wait channels: callers check their condition with interrupts disabled, then block. */
void process_block(const void *channel);
void process_wakeup(const void *channel);

struct proc_info {
    int pid;
    enum proc_state state;
    char name[16];
    int is_user;
};
int process_list(struct proc_info *out, int max);
const char *proc_state_name(enum proc_state s);

#endif
