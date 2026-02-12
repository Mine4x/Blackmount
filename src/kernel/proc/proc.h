#pragma once
#include <stdint.h>
#include <arch/x86_64/isr.h>

#define MAX_PROCESSES 64
#define PROC_STACK_SIZE 8192

typedef struct {
    uint32_t PID;
    uint32_t PPID;
    uint32_t Priority;
    uint64_t CPUTime;
} Proc_t;

void proc_init(void);
void proc_start_scheduling(void);
int  proc_create(void (*entry)(void), uint32_t priority, uint32_t parent);
void proc_schedule_interrupt(Registers* frame);
void proc_update_time(uint32_t ticks);
