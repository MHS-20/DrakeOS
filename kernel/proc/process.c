/* Process table, round-robin preemptive scheduler, kernel threads and user processes. */
#include <arch.h>
#include <console.h>
#include <cpu.h>
#include <elf.h>
#include <fs.h>
#include <layout.h>
#include <mm.h>
#include <proc.h>
#include <string.h>

extern void switch_context(uint32_t *old_esp, uint32_t new_esp);   /* context.asm */
extern void kthread_entry(void);
extern void user_entry(void);

static struct process procs[MAX_PROCS];
static struct process *current;
static int next_pid = 1;
static int slice = TIME_SLICE;
static int scheduler_running;

struct process *current_process(void) { return current; }

void sched_init(void)
{
    struct process *idle = &procs[0];
    idle->pid = 0;
    idle->state = P_RUNNING;
    strlcpy(idle->name, "idle", sizeof idle->name);
    idle->pd = vmm_kernel_directory();
    current = idle;
    scheduler_running = 1;
}

static struct process *alloc_process(const char *name)
{
    for (int i = 1; i < MAX_PROCS; i++) {
        if (procs[i].state == P_UNUSED) {
            struct process *p = &procs[i];
            memset(p, 0, sizeof *p);
            p->pid = next_pid++;
            strlcpy(p->name, name, sizeof p->name);
            p->pd = vmm_kernel_directory();
            p->parent = current;
            p->kstack = kmalloc(KSTACK_SIZE);
            if (!p->kstack)
                return NULL;
            p->kstack_top = (uint32_t)p->kstack + KSTACK_SIZE;
            return p;
        }
    }
    return NULL;
}

/* Releases everything a dead process owns. Never called for the running process. */
static void reap(struct process *p)
{
    KASSERT(p != current);
    if (p->is_user)
        vmm_destroy_address_space(p->pd);
    kfree(p->kstack);
    p->state = P_UNUSED;
}

static uint32_t *push(uint32_t *sp, uint32_t v)
{
    *--sp = v;
    return sp;
}

/* Builds the frame that switch_context pops: edi, esi, ebx, ebp, return address. */
static uint32_t *push_switch_frame(uint32_t *sp, void (*entry)(void), uint32_t ebx, uint32_t esi)
{
    sp = push(sp, (uint32_t)entry);
    sp = push(sp, 0);              /* ebp */
    sp = push(sp, ebx);
    sp = push(sp, esi);
    sp = push(sp, 0);              /* edi */
    return sp;
}

int kthread_create(const char *name, int (*fn)(void *), void *arg)
{
    uint32_t flags = irq_save();
    struct process *p = alloc_process(name);
    if (!p) {
        irq_restore(flags);
        return -1;
    }
    p->detached = 1;
    uint32_t *sp = (uint32_t *)p->kstack_top;
    p->esp = (uint32_t)push_switch_frame(sp, kthread_entry, (uint32_t)fn, (uint32_t)arg);
    p->fds[0] = vfs_open("/dev/console", O_READ);
    p->fds[1] = vfs_open("/dev/console", O_WRITE);
    p->fds[2] = vfs_dup(p->fds[1]);
    p->state = P_READY;
    irq_restore(flags);
    return p->pid;
}

/* Copies argv to the top of the new user stack: [strings][argv[]][NULL] then argc, argv. */
static uint32_t setup_user_stack(uint32_t pd, int argc, char *const argv[])
{
    uint32_t base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    uint32_t top_frame = 0;
    for (uint32_t va = base; va < USER_STACK_TOP; va += PAGE_SIZE) {
        uint32_t frame = pmm_alloc();
        if (!frame || vmm_map(pd, va, frame, PTE_USER | PTE_WRITE) < 0)
            return 0;
        top_frame = frame;
    }
    /* Only the topmost page is written here, through the kernel's linear map. */
    uint8_t *page = P2V(top_frame);
    uint32_t sp = USER_STACK_TOP, ptrs[16];
    if (argc > 15)
        argc = 15;
    for (int i = argc - 1; i >= 0; i--) {
        uint32_t len = strlen(argv[i]) + 1;
        if (len > 200)
            len = 200;
        sp -= len;
        memcpy(page + (sp - (USER_STACK_TOP - PAGE_SIZE)), argv[i], len);
        page[sp - (USER_STACK_TOP - PAGE_SIZE) + len - 1] = '\0';
        ptrs[i] = sp;
    }
    sp &= ~3u;
    sp -= 4;                                   /* argv[argc] = NULL */
    *(uint32_t *)(page + (sp - (USER_STACK_TOP - PAGE_SIZE))) = 0;
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 4;
        *(uint32_t *)(page + (sp - (USER_STACK_TOP - PAGE_SIZE))) = ptrs[i];
    }
    uint32_t argv_ptr = sp;
    sp = (sp - 8) & ~15u;                      /* crt0 finds argc at [esp], argv at [esp + 4] */
    *(uint32_t *)(page + (sp - (USER_STACK_TOP - PAGE_SIZE))) = argc;
    *(uint32_t *)(page + (sp + 4 - (USER_STACK_TOP - PAGE_SIZE))) = argv_ptr;
    return sp;
}

int process_spawn(const char *path, int argc, char *const argv[], int detached)
{
    void *image;
    uint32_t size;
    if (vfs_read_all(path, &image, &size) < 0)
        return -1;

    uint32_t pd = vmm_create_address_space();
    uint32_t entry = 0, user_sp = 0;
    int ok = pd && elf_load(pd, image, size, &entry) == 0 && (user_sp = setup_user_stack(pd, argc, argv));
    kfree(image);
    if (!ok) {
        if (pd)
            vmm_destroy_address_space(pd);
        return -1;
    }

    uint32_t flags = irq_save();
    const char *name = path;
    for (const char *s = path; *s; s++)
        if (*s == '/')
            name = s + 1;
    struct process *p = alloc_process(name);
    if (!p) {
        irq_restore(flags);
        vmm_destroy_address_space(pd);
        return -1;
    }
    p->is_user = 1;
    p->detached = detached;
    p->pd = pd;
    for (int i = 0; i < 3; i++)
        p->fds[i] = current->fds[i] ? vfs_dup(current->fds[i]) : NULL;

    /* iret frame for ring 3, below it the switch frame that "returns" into user_entry */
    uint32_t *sp = (uint32_t *)p->kstack_top;
    sp = push(sp, USER_DS);
    sp = push(sp, user_sp);
    sp = push(sp, 0x202);                      /* IF set */
    sp = push(sp, USER_CS);
    sp = push(sp, entry);
    p->esp = (uint32_t)push_switch_frame(sp, user_entry, 0, 0);
    p->state = P_READY;
    irq_restore(flags);
    klog(LOG_INFO, "spawned %s pid %d entry %08x", p->name, p->pid, entry);
    return p->pid;
}

void schedule(void)
{
    if (!scheduler_running)
        return;
    /* Clean up detached processes that exited (their stacks are no longer in use). */
    for (int i = 1; i < MAX_PROCS; i++)
        if (procs[i].state == P_ZOMBIE && procs[i].detached && &procs[i] != current)
            reap(&procs[i]);

    int start = current - procs;
    struct process *next = &procs[0];          /* idle if nothing else can run */
    for (int n = 1; n <= MAX_PROCS; n++) {
        struct process *p = &procs[(start + n) % MAX_PROCS];
        if (p != &procs[0] && (p->state == P_READY || (p == current && p->state == P_RUNNING))) {
            next = p;
            break;
        }
    }
    slice = TIME_SLICE;
    if (next == current)
        return;
    struct process *prev = current;
    if (prev->state == P_RUNNING)
        prev->state = P_READY;
    next->state = P_RUNNING;
    current = next;
    if (next->is_user)
        tss_set_kernel_stack(next->kstack_top);
    vmm_switch(next->pd);
    switch_context(&prev->esp, next->esp);
}

void sched_tick(void)
{
    if (!scheduler_running)
        return;
    uint32_t now = timer_ticks();
    for (int i = 1; i < MAX_PROCS; i++)
        if (procs[i].state == P_SLEEPING && (int32_t)(now - procs[i].wake_tick) >= 0)
            procs[i].state = P_READY;
    if (--slice <= 0 || current == &procs[0])
        schedule();
}

void sched_yield(void)
{
    uint32_t flags = irq_save();
    schedule();
    irq_restore(flags);
}

void process_block(const void *channel)
{
    current->wait_channel = channel;
    current->state = P_BLOCKED;
    schedule();
    current->wait_channel = NULL;
}

void process_wakeup(const void *channel)
{
    for (int i = 1; i < MAX_PROCS; i++)
        if (procs[i].state == P_BLOCKED && procs[i].wait_channel == channel)
            procs[i].state = P_READY;
}

void process_sleep(uint32_t ms)
{
    uint32_t flags = irq_save();
    current->wake_tick = timer_ticks() + (ms * TIMER_HZ + 999) / 1000;
    current->state = P_SLEEPING;
    schedule();
    irq_restore(flags);
}

void process_exit(int status)
{
    cli();
    struct process *p = current;
    KASSERT(p != &procs[0]);
    for (int i = 0; i < MAX_FDS; i++)
        if (p->fds[i]) {
            vfs_close(p->fds[i]);
            p->fds[i] = NULL;
        }
    p->exit_status = status;
    p->state = P_ZOMBIE;
    for (int i = 1; i < MAX_PROCS; i++)        /* orphans are reaped automatically */
        if (procs[i].parent == p)
            procs[i].detached = 1, procs[i].parent = NULL;
    klog(LOG_INFO, "pid %d (%s) exited with %d", p->pid, p->name, status);
    process_wakeup(p);
    schedule();
    panic("zombie process was scheduled");
}

int process_wait(int pid)
{
    struct process *child = NULL;
    for (int i = 1; i < MAX_PROCS; i++)
        if (procs[i].state != P_UNUSED && procs[i].pid == pid && procs[i].parent == current)
            child = &procs[i];
    if (!child || child->detached)
        return -1;
    uint32_t flags = irq_save();
    while (child->state != P_ZOMBIE)
        process_block(child);
    int status = child->exit_status;
    reap(child);
    irq_restore(flags);
    return status;
}

int process_kill(int pid)
{
    uint32_t flags = irq_save();
    for (int i = 1; i < MAX_PROCS; i++) {
        struct process *p = &procs[i];
        if (p->state == P_UNUSED || p->state == P_ZOMBIE || p->pid != pid)
            continue;
        if (!p->is_user) {
            irq_restore(flags);
            return -1;                          /* kernel threads cannot be killed */
        }
        p->killed = 1;
        if (p->state == P_BLOCKED || p->state == P_SLEEPING)
            p->state = P_READY;
        irq_restore(flags);
        return 0;
    }
    irq_restore(flags);
    return -1;
}

void process_kill_current(uint32_t reason)
{
    kprintf("%s (pid %d) killed by exception %u\n", current->name, current->pid, reason);
    current->killed = 1;
}

/* Runs at the end of every interrupt: a killed user process exits instead of returning to ring 3. */
void process_check_signals(struct registers *r)
{
    if (current && current->killed && (r->cs & 3) == 3)
        process_exit(-1);
}

const char *proc_state_name(enum proc_state s)
{
    static const char *const names[] = { "unused", "ready", "running", "blocked", "sleeping", "zombie" };
    return names[s];
}

int process_list(struct proc_info *out, int max)
{
    int n = 0;
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_PROCS && n < max; i++) {
        if (procs[i].state == P_UNUSED)
            continue;
        out[n].pid = procs[i].pid;
        out[n].state = procs[i].state;
        out[n].is_user = procs[i].is_user;
        strlcpy(out[n].name, procs[i].name, sizeof out[n].name);
        n++;
    }
    irq_restore(flags);
    return n;
}
