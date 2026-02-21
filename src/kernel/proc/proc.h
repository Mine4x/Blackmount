#pragma once
#include <stdint.h>
#include <arch/x86_64/isr.h>

#define MAX_PROCESSES   64
#define PROC_STACK_SIZE 8192
#define USER_CODE_BASE  0x400000
#define USER_STACK_BASE 0x800000

typedef enum {
    PROC_TYPE_KERNEL = 0,
    PROC_TYPE_USER   = 1
} ProcType;

typedef struct {
    uint32_t PID;
    uint32_t PPID;
    uint32_t Priority;
    uint64_t CPUTime;
    ProcType Type;
} Proc_t;

void proc_init(void);

// Starts scheduling. Automatically enters the first available user task,
// or falls back to the idle loop if only kernel tasks exist.
void proc_start_scheduling(void);

int  proc_create_kernel(void (*entry)(void), uint32_t priority, uint32_t parent);
int  proc_create_user(void (*entry)(void), void (*end_marker)(void), uint32_t priority, uint32_t parent);

// Mark the current process as exited and switch to the next ready process.
// Call from a syscall handler (kernel mode). Does not return.
void proc_exit(void);

void proc_schedule_interrupt(Registers* frame);
void proc_update_time(uint32_t ticks);

int  proc_get_current_pid(void);

#define USER_PROGRAM_END() void __user_program_end_##__LINE__(void) {}