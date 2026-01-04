#include <stdint.h>
#include <arch/i686/irq.h>
#include <debug.h>

#define PIT_FREQUENCY 1193182  // PIT oscillator frequency in Hz
#define TARGET_FREQUENCY 1000  // Target 1000 Hz (1ms per tick)

volatile uint32_t g_pit_ticks = 0;

static void timer_irq_handler(Registers* regs) {
    g_pit_ticks++;
}

void timer_init() {
    // Calculate divisor for desired frequency
    uint16_t divisor = PIT_FREQUENCY / TARGET_FREQUENCY;
    
    // Send command byte: Channel 0, lobyte/hibyte, rate generator
    i686_outb(0x43, 0x36);
    
    // Send divisor
    i686_outb(0x40, divisor & 0xFF);        // Low byte
    i686_outb(0x40, (divisor >> 8) & 0xFF); // High byte
    
    // Register IRQ handler for IRQ 0 (timer)
    i686_IRQ_RegisterHandler(0, timer_irq_handler);
    
    log_ok("TIMER", "PIT initialized at %d Hz (1ms ticks)", TARGET_FREQUENCY);
}

uint32_t timer_get_ticks() {
    return g_pit_ticks;
}