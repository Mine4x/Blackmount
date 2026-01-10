#include "proc.h"
#include <stddef.h>
#include <string.h>
#include <debug.h>

#define MAX_PROCESSES 64
#define PROC_STACK_SIZE 4096

// Process states
typedef enum {
    PROC_STATE_UNUSED = 0,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_BLOCKED,
    PROC_STATE_ZOMBIE
} ProcState;

// CPU context for saving/restoring registers
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, esp, ebp;
    uint32_t eip, eflags;
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
    while (1)
    {}
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
    while(1);
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
    
    stack_top -= 4;
    *(uint32_t*)stack_top = 0;
    
    pcb->proc.SP = stack_top;
    
    memset(&pcb->context, 0, sizeof(Context));
    pcb->context.esp = stack_top;
    pcb->context.ebp = 0;
    pcb->context.eip = (uint32_t)proc_entry_wrapper;
    pcb->context.eflags = 0x202;
    
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

void proc_schedule(void) {
    if (!scheduling_enabled) {
        return;
    }
    
    int old_proc = current_proc_index;
    int start = current_proc_index;
    int next = (current_proc_index + 1) % MAX_PROCESSES;
    
    while (next != start) {
        if (proc_table[next].state == PROC_STATE_READY) {
            current_proc_index = next;
            proc_table[next].state = PROC_STATE_RUNNING;
            
            if (old_proc >= 0 && proc_table[old_proc].state == PROC_STATE_RUNNING) {
                proc_table[old_proc].state = PROC_STATE_READY;
            }
            
            if (old_proc >= 0 && old_proc != next) {
                context_switch(&proc_table[old_proc].context, 
                             &proc_table[next].context);
            }
            return;
        }
        next = (next + 1) % MAX_PROCESSES;
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