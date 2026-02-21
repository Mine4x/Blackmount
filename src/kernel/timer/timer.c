#include <stdint.h>
#include <arch/x86_64/irq.h>
#include <arch/x86_64/io.h>
#include <debug.h>
#include <proc/proc.h>
#include <drivers/disk/floppy.h>

#define PIT_FREQUENCY    1193182
#define TARGET_FREQUENCY 100

volatile uint32_t g_pit_ticks = 0;

static void timer_irq_handler(Registers* regs) {
    g_pit_ticks++;

    proc_update_time(1);
    proc_schedule_interrupt(regs);
}

void timer_init() {
    uint16_t divisor = PIT_FREQUENCY / TARGET_FREQUENCY;

    x86_64_outb(0x43, 0x36);
    x86_64_outb(0x40, divisor & 0xFF);
    x86_64_outb(0x40, (divisor >> 8) & 0xFF);
    
    x86_64_IRQ_RegisterHandler(0, timer_irq_handler);
    x86_64_IRQ_Unmask(0);
    
    log_ok("TIMER", "PIT initialized at %d Hz", TARGET_FREQUENCY);
}

uint32_t timer_get_ticks() {
    return g_pit_ticks;
}

void timer_sleep_us(uint32_t us) {
    if (!us) return;
    while (us > 0) {
        uint32_t chunk = (us > 54000) ? 54000 : us;
        uint32_t ticks = (uint32_t)((uint64_t)chunk * PIT_FREQUENCY / 1000000UL);
        if (!ticks) ticks = 1;

        x86_64_outb(0x43, 0b10110000);
        x86_64_outb(0x42, (uint8_t)(ticks & 0xFF));
        x86_64_outb(0x42, (uint8_t)(ticks >> 8));

        uint8_t ctrl = x86_64_inb(0x61);
        ctrl = (ctrl & ~0x02) | 0x01;
        x86_64_outb(0x61, ctrl);

        while (!(x86_64_inb(0x61) & 0x20))
            __asm__ volatile("pause" ::: "memory");

        us -= chunk;
    }
}

void timer_sleep_ms(uint32_t ms) {
    timer_sleep_us(ms * 1000);
}

void timer_sleep_s(uint32_t s) {
    timer_sleep_us(s * 1000000);
}