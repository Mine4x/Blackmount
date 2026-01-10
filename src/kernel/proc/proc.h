// proc.h
#ifndef PROC_H
#define PROC_H

#include <stdint.h>

typedef struct Proc_t {
    uint32_t PID;       // Process id
    uint32_t PPID;      // Parent's PID
    uintptr_t SP;       // Stack pointer
    uint32_t Priority;  // Priority
    uint32_t CPUTime;   // CPU time the process took
} Proc_t;

void proc_init(void);
void proc_start_scheduling(void);
int proc_create(void (*entry_point)(void), uint32_t priority, uint32_t parent_pid);
Proc_t* proc_get(uint32_t pid);
Proc_t* proc_current(void);
int proc_kill(uint32_t pid);
void proc_schedule(void);
void proc_yield(void);
void proc_block(void);
int proc_unblock(uint32_t pid);
void proc_update_time(uint32_t ticks);
int proc_count(void);
void proc_list(void (*callback)(Proc_t* proc, const char* state));

#endif // PROC_H