#include <stdint.h>
#include <arch/x86_64/irq.h>
#include <arch/x86_64/io.h>
#include <debug.h>
#include <proc/proc.h>
#include <drivers/disk/floppy.h>

#define PIT_FREQUENCY 1193182  // PIT oscillator frequency in Hz
#define TARGET_FREQUENCY 100 // Target 1000 Hz (1ms per tick)

volatile uint32_t g_pit_ticks = 0;

static void timer_irq_handler(Registers* regs)
{
    g_pit_ticks++;

    proc_update_time(1);
    proc_schedule_interrupt(regs);
}

void timer_init() {
    uint16_t divisor = PIT_FREQUENCY / TARGET_FREQUENCY;
    
    x86_64_outb(0x43, 0x36);
    x86_64_outb(0x40, divisor & 0xFF);
    x86_64_outb(0x40, (divisor >> 8) & 0xFF);
    
    // Register handler FIRST
    x86_64_IRQ_RegisterHandler(0, timer_irq_handler);
    
    // Unmask IRQ 0
    x86_64_IRQ_Unmask(0);
    
    log_ok("TIMER", "PIT initialized at %d Hz", TARGET_FREQUENCY);
}

uint32_t timer_get_ticks() {
    return g_pit_ticks;
}