#pragma once
#include <stdint.h>
#include <arch/x86_64/isr.h>
#include <stddef.h>

#define MAX_PROCESSES 64
#define PROC_STACK_SIZE 8192
#define USER_CODE_BASE  0x400000  // User code starts here
#define USER_STACK_BASE 0x800000  // User stacks start here (grow down)

typedef enum {
    PROC_TYPE_KERNEL = 0,
    PROC_TYPE_USER = 1
} ProcType;

typedef struct {
    uint32_t PID;
    uint32_t PPID;
    uint32_t Priority;
    uint64_t CPUTime;
    ProcType Type;
} Proc_t;

void proc_init(void);
void proc_start_scheduling(void);

// Create kernel task - entry is kernel function pointer
int proc_create_kernel(void (*entry)(void), uint32_t priority, uint32_t parent);

// Create user task - entry_addr is user virtual address (0x400000+)
// You must ensure code is loaded at this address and pages are user-accessible
int proc_create_user(uint64_t entry_addr, uint32_t priority, uint32_t parent);

// Load user code into user space
int proc_load_user_code(int pid, const void* code, size_t size, uint64_t dest_addr);

// Run a user task (enter user mode - doesn't return normally)
void proc_run_user_task(int pid);

void proc_schedule_interrupt(Registers* frame);
void proc_update_time(uint32_t ticks);

// Get current process info
int proc_get_current_pid(void);