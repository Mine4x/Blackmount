#pragma once
#include <stdint.h>
#include <arch/x86_64/isr.h>

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

// Create user task from kernel function - automatically copies code to user space
// Use USER_PROGRAM_END() macro after your function to mark the end
int proc_create_user(void (*entry)(void), void (*end_marker)(void), uint32_t priority, uint32_t parent);

// Run a user task (enter user mode - doesn't return normally)
void proc_run_user_task(int pid);

void proc_schedule_interrupt(Registers* frame);
void proc_update_time(uint32_t ticks);

// Get current process info
int proc_get_current_pid(void);

// Macro to mark the end of a user program
#define USER_PROGRAM_END() void __user_program_end_##__LINE__(void) {}