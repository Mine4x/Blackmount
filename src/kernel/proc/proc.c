#include "proc.h"
#include <string.h>
#include <arch/x86_64/isr.h>
#include <arch/x86_64/gdt.h>
#include <memory.h>
#include <mem/vmm.h>
#include <debug.h>

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_ZOMBIE
} ProcState;

typedef struct {
    Proc_t proc;
    ProcState state;
    Registers context;

    uint8_t  kernel_stack[PROC_STACK_SIZE];
    uint64_t kernel_stack_top;

    uint64_t user_code_base;
    uint64_t user_stack_base;
    uint64_t user_stack_top;
} PCB;

static PCB      proc_table[MAX_PROCESSES];
static int      current_proc       = -1;
static uint32_t next_pid           = 1;
static int      scheduling_enabled = 0;
static uint64_t next_user_code_addr = USER_CODE_BASE;

extern void enter_usermode(uint64_t entry, uint64_t stack);

static void idle(void)
{
    while (1)
        __asm__ volatile("hlt");
}

static int find_next(void)
{
    if (current_proc < 0)
        return -1;

    int start = current_proc;
    int i = (start + 1) % MAX_PROCESSES;

    while (i != start) {
        if (proc_table[i].state == PROC_READY)
            return i;
        i = (i + 1) % MAX_PROCESSES;
    }

    if (proc_table[current_proc].state == PROC_RUNNING)
        return current_proc;

    return -1;
}

static int proc_create_internal(uint64_t entry, uint32_t priority, uint32_t parent, ProcType type)
{
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }

    if (slot < 0)
        return -1;

    PCB* pcb = &proc_table[slot];
    memset(pcb, 0, sizeof(PCB));

    pcb->proc.PID      = next_pid++;
    pcb->proc.PPID     = parent;
    pcb->proc.Priority = priority;
    pcb->proc.Type     = type;
    pcb->state         = PROC_READY;

    uint64_t kernel_stack_top = (uint64_t)(pcb->kernel_stack + PROC_STACK_SIZE);
    kernel_stack_top &= ~0xFULL;
    pcb->kernel_stack_top = kernel_stack_top;

    memset(&pcb->context, 0, sizeof(Registers));
    pcb->context.rip       = entry;
    pcb->context.rflags    = 0x202;
    pcb->context.interrupt = 0;
    pcb->context.error     = 0;

    if (type == PROC_TYPE_KERNEL) {
        pcb->context.cs  = x86_64_GDT_CODE_SEGMENT;
        pcb->context.ss  = x86_64_GDT_DATA_SEGMENT;
        pcb->context.rsp = kernel_stack_top;

        pcb->user_code_base  = 0;
        pcb->user_stack_base = 0;
        pcb->user_stack_top  = 0;

    } else {
        pcb->context.cs = x86_64_GDT_USER_CODE_SEGMENT | 3;
        pcb->context.ss = x86_64_GDT_USER_DATA_SEGMENT | 3;

        pcb->user_code_base  = entry;

        pcb->user_stack_base = USER_STACK_BASE - ((slot + 1) * PROC_STACK_SIZE * 2);
        pcb->user_stack_top  = pcb->user_stack_base + PROC_STACK_SIZE;
        pcb->user_stack_top &= ~0xFULL;

        pcb->context.rsp = pcb->user_stack_top;
    }

    return pcb->proc.PID;
}

void proc_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    current_proc        = -1;
    next_pid            = 1;
    scheduling_enabled  = 0;
    next_user_code_addr = USER_CODE_BASE;

    proc_create_kernel(idle, 0, 0);
}

void proc_start_scheduling(void)
{
    scheduling_enabled = 1;

    // Prefer entering the first ready user task directly via enter_usermode.
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_READY &&
            proc_table[i].proc.Type == PROC_TYPE_USER) {
            current_proc = i;
            proc_table[i].state = PROC_RUNNING;
            x86_64_TSS_SetKernelStack(proc_table[i].kernel_stack_top);
            log_info("PROC", "Starting user task PID %d rip=0x%lx rsp=0x%lx",
                     proc_table[i].proc.PID,
                     proc_table[i].context.rip,
                     proc_table[i].context.rsp);
            enter_usermode(proc_table[i].context.rip, proc_table[i].context.rsp);
            // does not return
        }
    }

    // No user tasks yet — idle until the timer schedules one.
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_READY) {
            current_proc = i;
            proc_table[i].state = PROC_RUNNING;
            break;
        }
    }
}

int proc_create_kernel(void (*entry)(void), uint32_t priority, uint32_t parent)
{
    return proc_create_internal((uint64_t)entry, priority, parent, PROC_TYPE_KERNEL);
}

int proc_create_user(void (*entry)(void), void (*end_marker)(void), uint32_t priority, uint32_t parent)
{
    __asm__ volatile("cli");

    if (!entry || !end_marker) {
        __asm__ volatile("sti");
        log_err("PROC", "Invalid function pointers!");
        return -1;
    }

    size_t code_size = (uint64_t)end_marker - (uint64_t)entry;

    if (code_size == 0 || code_size > 0x10000) {
        __asm__ volatile("sti");
        log_err("PROC", "Invalid user program size: %d bytes", code_size);
        return -1;
    }

    size_t pages_needed = (code_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t alloc_size   = pages_needed * PAGE_SIZE;
    uint64_t user_addr  = next_user_code_addr;

    if (user_addr + alloc_size >= USER_STACK_BASE) {
        __asm__ volatile("sti");
        log_err("PROC", "Out of user space!");
        return -1;
    }

    uint64_t safe_addr = user_addr;
    size_t   safe_size = code_size;

    __asm__ volatile("sti");
    log_info("PROC", "Copying %d bytes of user code to 0x%lx", safe_size, safe_addr);
    __asm__ volatile("cli");

    memcpy((void*)user_addr, (void*)entry, code_size);

    if (alloc_size > code_size)
        memset((void*)(user_addr + code_size), 0, alloc_size - code_size);

    int pid = proc_create_internal(user_addr, priority, parent, PROC_TYPE_USER);

    if (pid < 0) {
        __asm__ volatile("sti");
        log_err("PROC", "Failed to create user task!");
        return -1;
    }

    next_user_code_addr = user_addr + alloc_size;

    __asm__ volatile("sti");
    log_ok("PROC", "Created user task PID %d at 0x%lx", pid, safe_addr);

    return pid;
}

// Called from a syscall handler (kernel mode) to exit the current process.
// Cleans up the PCB and switches directly into the next runnable process.
void __attribute__((noreturn)) proc_exit(void)
{
    if (current_proc < 0)
        goto halt;

    int exiting = current_proc;
    log_info("PROC", "Process PID %d exiting", proc_table[exiting].proc.PID);
    proc_table[exiting].state = PROC_ZOMBIE;

    int next = -1;
    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int candidate = (exiting + i) % MAX_PROCESSES;
        if (proc_table[candidate].state == PROC_READY) {
            next = candidate;
            break;
        }
    }

    memset(&proc_table[exiting], 0, sizeof(PCB));

    if (next < 0)
        goto halt;

    current_proc = next;
    proc_table[next].state = PROC_RUNNING;

    if (proc_table[next].proc.Type == PROC_TYPE_USER) {
        x86_64_TSS_SetKernelStack(proc_table[next].kernel_stack_top);
        enter_usermode(proc_table[next].context.rip, proc_table[next].context.rsp);
    }

    // Kernel task (e.g. idle) — just enable interrupts and hlt loop.
    // The timer will schedule properly from here.
halt:
    log_info("PROC", "All user tasks exited, idling");
    current_proc = next; // may be -1 or idle slot, both are fine
    __asm__ volatile("sti");
    while (1) __asm__ volatile("hlt");

    __builtin_unreachable();
}

void proc_schedule_interrupt(Registers* frame)
{
    if (!scheduling_enabled)
        return;

    int old = current_proc;

    if (old >= 0 && proc_table[old].state == PROC_RUNNING) {
        proc_table[old].context = *frame;
        proc_table[old].state   = PROC_READY;
    }

    int next = find_next();
    if (next < 0)
        return;

    current_proc = next;
    proc_table[next].state = PROC_RUNNING;

    if (proc_table[next].proc.Type == PROC_TYPE_USER)
        x86_64_TSS_SetKernelStack(proc_table[next].kernel_stack_top);

    *frame = proc_table[next].context;
}

void proc_update_time(uint32_t ticks)
{
    if (current_proc >= 0)
        proc_table[current_proc].proc.CPUTime += ticks;
}

int proc_get_current_pid(void)
{
    if (current_proc < 0)
        return -1;

    return proc_table[current_proc].proc.PID;
}