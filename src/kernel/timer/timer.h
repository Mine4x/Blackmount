#ifndef TIMER_H
#define TIMER_H
#include <arch/x86_64/isr.h>

#include <stdint.h>

extern volatile uint32_t g_pit_ticks;

void timer_init();
void timer_irq_handler(Registers* regs);
uint32_t timer_get_ticks();

void timer_sleep_us(uint32_t us);
void timer_sleep_ms(uint32_t ms);
void timer_sleep_s(uint32_t s);
#endif