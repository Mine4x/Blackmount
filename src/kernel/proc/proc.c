#include "proc.h"
#include <string.h>
#include <arch/x86_64/isr.h>

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED
} ProcState;

typedef struct {
    Proc_t proc;
    ProcState state;
    Registers context;
    uint8_t stack[PROC_STACK_SIZE];
} PCB;

static PCB proc_table[MAX_PROCESSES];
static int current_proc = -1;
static uint32_t next_pid = 1;
static int scheduling_enabled = 0;

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

void proc_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    current_proc = -1;
    next_pid = 1;
    scheduling_enabled = 0;

    proc_create(idle, 0, 0);
}

void proc_start_scheduling(void)
{
    scheduling_enabled = 1;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_READY) {
            current_proc = i;
            proc_table[i].state = PROC_RUNNING;
            break;
        }
    }
}

int proc_create(void (*entry)(void), uint32_t priority, uint32_t parent)
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

    pcb->proc.PID = next_pid++;
    pcb->proc.PPID = parent;
    pcb->proc.Priority = priority;
    pcb->state = PROC_READY;

    uint64_t stack_top =
        (uint64_t)(pcb->stack + PROC_STACK_SIZE);

    stack_top &= ~0xFULL;

    memset(&pcb->context, 0, sizeof(Registers));

    pcb->context.rip = (uint64_t)entry;
    pcb->context.rsp = stack_top;

    pcb->context.rflags = 0x202;  // IF enabled
    pcb->context.cs = 0x08;       // kernel code segment
    pcb->context.ss = 0x10;       // kernel data segment

    pcb->context.interrupt = 0;
    pcb->context.error = 0;

    return pcb->proc.PID;
}

void proc_schedule_interrupt(Registers* frame)
{
    if (!scheduling_enabled)
        return;

    int old = current_proc;

    // Save current process context
    if (old >= 0 &&
        proc_table[old].state == PROC_RUNNING)
    {
        proc_table[old].context = *frame;
        proc_table[old].state = PROC_READY;
    }

    int next = find_next();
    if (next < 0)
        return;

    current_proc = next;
    proc_table[next].state = PROC_RUNNING;

    // Overwrite interrupt frame
    *frame = proc_table[next].context;
}

void proc_update_time(uint32_t ticks)
{
    if (current_proc >= 0)
        proc_table[current_proc].proc.CPUTime += ticks;
}
