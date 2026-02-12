#include "proc.h"
#include <stddef.h>
#include <string.h>
#include <debug.h>

// TODO: Make this preemtive.

#define MAX_PROCESSES 64
#define PROC_STACK_SIZE 8192

// Process states
typedef enum {
    PROC_STATE_UNUSED = 0,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_BLOCKED,
    PROC_STATE_ZOMBIE
} ProcState;
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rsp, rbp;
    uint64_t rip, rflags;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
} Context;

// Internal process structure
typedef struct {
    Proc_t proc;                    // Public process info
    ProcState state;                // Process state
    Context context;                // Saved CPU context
    uint8_t stack[PROC_STACK_SIZE]; // Process stack
    void (*entry_point)(void);      // Entry function
} ProcessControlBlock;

// Process table
static ProcessControlBlock proc_table[MAX_PROCESSES];
static uint32_t next_pid = 1;
static int current_proc_index = -1;
static int scheduling_enabled = 0;

// Assembly function to perform actual context switch
extern void context_switch(Context* old_ctx, Context* new_ctx);

static void idle(void) {
    while (1) {
        __asm__ __volatile__("hlt");  // Halt until next interrupt
    }
}

// Process entry wrapper
static void proc_entry_wrapper(void) {
    if (current_proc_index >= 0 && current_proc_index < MAX_PROCESSES) {
        ProcessControlBlock* pcb = &proc_table[current_proc_index];
        if (pcb->entry_point) {
            pcb->entry_point();
        }
    }
    if (current_proc_index >= 0) {
        proc_kill(proc_table[current_proc_index].proc.PID);
    }
    proc_yield();
    while(1) {
        __asm__ __volatile__("hlt");
    }
}

void proc_init(void) {
    memset(proc_table, 0, sizeof(proc_table));
    next_pid = 1;
    current_proc_index = -1;
    scheduling_enabled = 0;

    proc_create(idle, 1, 0);
}

void proc_start_scheduling(void) {
    scheduling_enabled = 1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_STATE_READY) {
            current_proc_index = i;
            proc_table[i].state = PROC_STATE_RUNNING;
            break;
        }
    }
}

// Create a new process
int proc_create(void (*entry_point)(void), uint32_t priority, uint32_t parent_pid) {
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_STATE_UNUSED) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        return -1;
    }
    
    ProcessControlBlock* pcb = &proc_table[slot];
    
    pcb->proc.PID = next_pid++;
    pcb->proc.PPID = parent_pid;
    pcb->proc.Priority = priority;
    pcb->proc.CPUTime = 0;
    
    uintptr_t stack_top = (uintptr_t)(pcb->stack + PROC_STACK_SIZE);
    stack_top &= ~0xF;
    
    stack_top -= 8;
    *(uint64_t*)stack_top = 0;
    
    pcb->proc.SP = stack_top;
    
    memset(&pcb->context, 0, sizeof(Context));
    pcb->context.rsp = stack_top;
    pcb->context.rbp = 0;
    pcb->context.rip = (uint64_t)proc_entry_wrapper;
    pcb->context.rflags = 0x202;
    
    pcb->entry_point = entry_point;
    pcb->state = PROC_STATE_READY;
    
    return pcb->proc.PID;
}

Proc_t* proc_get(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROC_STATE_UNUSED && 
            proc_table[i].proc.PID == pid) {
            return &proc_table[i].proc;
        }
    }
    return NULL;
}

Proc_t* proc_current(void) {
    if (current_proc_index >= 0 && current_proc_index < MAX_PROCESSES) {
        return &proc_table[current_proc_index].proc;
    }
    return NULL;
}

int proc_kill(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROC_STATE_UNUSED && 
            proc_table[i].proc.PID == pid) {
            
            for (int j = 0; j < MAX_PROCESSES; j++) {
                if (proc_table[j].proc.PPID == pid) {
                    proc_kill(proc_table[j].proc.PID);
                }
            }
            
            proc_table[i].state = PROC_STATE_UNUSED;
            memset(&proc_table[i], 0, sizeof(ProcessControlBlock));
            
            return 0;
        }
    }
    return -1;
}

static int find_next_process(void) {
    int start = current_proc_index;
    int next = (current_proc_index + 1) % MAX_PROCESSES;
    
    while (next != start) {
        if (proc_table[next].state == PROC_STATE_READY) {
            return next;
        }
        next = (next + 1) % MAX_PROCESSES;
    }
    
    if (current_proc_index >= 0 && 
        proc_table[current_proc_index].state == PROC_STATE_RUNNING) {
        return current_proc_index;
    }
    
    return -1;
}

void proc_schedule_interrupt_safe(
    uint64_t* rax, uint64_t* rbx, uint64_t* rcx, uint64_t* rdx,
    uint64_t* rsi, uint64_t* rdi, uint64_t* rsp, uint64_t* rbp,
    uint64_t* rip, uint64_t* rflags,
    uint64_t* r8, uint64_t* r9, uint64_t* r10, uint64_t* r11,
    uint64_t* r12, uint64_t* r13, uint64_t* r14, uint64_t* r15)
{
    if (!scheduling_enabled) {
        return;
    }
    
    int old_proc = current_proc_index;
    
    if (old_proc >= 0 && old_proc < MAX_PROCESSES) {
        if (proc_table[old_proc].state == PROC_STATE_RUNNING) {
            proc_table[old_proc].context.rax = *rax;
            proc_table[old_proc].context.rbx = *rbx;
            proc_table[old_proc].context.rcx = *rcx;
            proc_table[old_proc].context.rdx = *rdx;
            proc_table[old_proc].context.rsi = *rsi;
            proc_table[old_proc].context.rdi = *rdi;
            proc_table[old_proc].context.rsp = *rsp;
            proc_table[old_proc].context.rbp = *rbp;
            proc_table[old_proc].context.rip = *rip;
            proc_table[old_proc].context.rflags = *rflags;
            proc_table[old_proc].context.r8 = *r8;
            proc_table[old_proc].context.r9 = *r9;
            proc_table[old_proc].context.r10 = *r10;
            proc_table[old_proc].context.r11 = *r11;
            proc_table[old_proc].context.r12 = *r12;
            proc_table[old_proc].context.r13 = *r13;
            proc_table[old_proc].context.r14 = *r14;
            proc_table[old_proc].context.r15 = *r15;
            
            proc_table[old_proc].state = PROC_STATE_READY;
        }
    }
    
    int next = find_next_process();
    
    if (next >= 0 && next < MAX_PROCESSES) {
        current_proc_index = next;
        proc_table[next].state = PROC_STATE_RUNNING;
        
        *rax = proc_table[next].context.rax;
        *rbx = proc_table[next].context.rbx;
        *rcx = proc_table[next].context.rcx;
        *rdx = proc_table[next].context.rdx;
        *rsi = proc_table[next].context.rsi;
        *rdi = proc_table[next].context.rdi;
        *rsp = proc_table[next].context.rsp;
        *rbp = proc_table[next].context.rbp;
        *rip = proc_table[next].context.rip;
        *rflags = proc_table[next].context.rflags;
        *r8 = proc_table[next].context.r8;
        *r9 = proc_table[next].context.r9;
        *r10 = proc_table[next].context.r10;
        *r11 = proc_table[next].context.r11;
        *r12 = proc_table[next].context.r12;
        *r13 = proc_table[next].context.r13;
        *r14 = proc_table[next].context.r14;
        *r15 = proc_table[next].context.r15;
    }
}

void proc_schedule_interrupt(void* interrupt_frame) {
    if (!scheduling_enabled) {
        return;
    }
    
    uint64_t* regs = (uint64_t*)interrupt_frame;
    
    proc_schedule_interrupt_safe(
        &regs[0],  // rax
        &regs[1],  // rbx
        &regs[2],  // rcx
        &regs[3],  // rdx
        &regs[4],  // rsi
        &regs[5],  // rdi
        &regs[6],  // rsp
        &regs[7],  // rbp
        &regs[8],  // rip
        &regs[9],  // rflags
        &regs[10], // r8
        &regs[11], // r9
        &regs[12], // r10
        &regs[13], // r11
        &regs[14], // r12
        &regs[15], // r13
        &regs[16], // r14
        &regs[17]  // r15
    );
}

void proc_schedule(void) {
    if (!scheduling_enabled) {
        return;
    }
    
    int old_proc = current_proc_index;
    
    // Find next process
    int next = find_next_process();
    
    if (next >= 0 && next < MAX_PROCESSES) {
        current_proc_index = next;
        proc_table[next].state = PROC_STATE_RUNNING;
        
        if (old_proc >= 0 && proc_table[old_proc].state == PROC_STATE_RUNNING) {
            proc_table[old_proc].state = PROC_STATE_READY;
        }
        
        if (old_proc >= 0 && old_proc != next) {
            context_switch(&proc_table[old_proc].context, 
                         &proc_table[next].context);
        }
    }
}

void proc_yield(void) {
    if (current_proc_index >= 0) {
        proc_table[current_proc_index].state = PROC_STATE_READY;
    }
    proc_schedule();
}

void proc_block(void) {
    if (current_proc_index >= 0) {
        proc_table[current_proc_index].state = PROC_STATE_BLOCKED;
    }
    proc_schedule();
}

int proc_unblock(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_STATE_BLOCKED && 
            proc_table[i].proc.PID == pid) {
            proc_table[i].state = PROC_STATE_READY;
            return 0;
        }
    }
    return -1;
}

void proc_update_time(uint32_t ticks) {
    if (current_proc_index >= 0) {
        proc_table[current_proc_index].proc.CPUTime += ticks;
    }
}

int proc_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROC_STATE_UNUSED) {
            count++;
        }
    }
    return count;
}

void proc_list(void (*callback)(Proc_t* proc, const char* state)) {
    const char* states[] = {"UNUSED", "READY", "RUNNING", "BLOCKED", "ZOMBIE"};
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROC_STATE_UNUSED) {
            callback(&proc_table[i].proc, states[proc_table[i].state]);
        }
    }
}