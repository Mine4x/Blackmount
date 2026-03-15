#pragma once
#include <stdint.h>
#include <arch/x86_64/isr.h>
#include <stdbool.h>

#define MAX_PROCESSES    64
#define PROC_STACK_SIZE  8192

#define USER_CODE_BASE    0x00400000ULL
#define USER_CODE_LIMIT   0x20000000ULL
#define USER_STACK_ARENA  0x20000000ULL
#define USER_STACK_VSIZE  0x01000000ULL
#define USER_SPACE_END    0x40400000ULL

typedef enum { PROC_TYPE_KERNEL = 0, PROC_TYPE_USER = 1 } ProcType;

typedef struct {
    uint32_t PID;
    uint32_t PPID;
    uint32_t Priority;
    uint64_t CPUTime;
    ProcType Type;
} Proc_t;

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_ZOMBIE,
    PROC_BLOCKED
} ProcState;

void proc_init(void);
void proc_start_scheduling(void);
int  proc_create_kernel(void (*entry)(void), uint32_t priority, uint32_t parent);
int  proc_create_user(void (*entry)(void), void (*end_marker)(void), uint32_t priority, uint32_t parent);
void proc_exit(uint64_t exit_code);
void proc_schedule_interrupt(Registers* frame);
void proc_update_time(uint32_t ticks);
int  proc_get_current_pid(void);
void proc_block(int pid);
void proc_unblock(int pid);
void proc_yield(void);
void proc_enter_syscall(void);
void proc_exit_syscall(void);
bool proc_is_blocked(int pid);
bool proc_is_valid_demand_addr(uint64_t vaddr);

#define USER_PROGRAM_END() void __user_program_end_##__LINE__(void) {}