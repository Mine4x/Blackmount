#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>
#include <arch/x86_64/isr.h>


#define LAPIC_SPURIOUS_VECTOR   0xFF
#define LAPIC_TIMER_VECTOR      0xEF
#define LAPIC_ERROR_VECTOR      0xEB


typedef enum {
    LAPIC_TIMER_ONESHOT  = 0,
    LAPIC_TIMER_PERIODIC = 1,
} LAPICTimerMode;

void lapic_init(void);

// Send EOI — call at END of every LAPIC-sourced interrupt handler
void lapic_eoi(void);

// Read this CPU's APIC ID
uint32_t lapic_id(void);

// Timer — calibrates against PIT on first call
void lapic_timer_start(LAPICTimerMode mode, uint32_t ms);
void lapic_timer_stop(void);

// Send an IPI (fixed delivery, physical destination)
void lapic_send_ipi(uint32_t dest_apic_id, uint8_t vector);

void lapic_timer_handler(Registers* regs);

#endif