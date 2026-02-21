#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

extern volatile uint32_t g_pit_ticks;

void timer_init();
uint32_t timer_get_ticks();

void timer_sleep_us(uint32_t us);
void timer_sleep_ms(uint32_t ms);
void timer_sleep_s(uint32_t s);
#endif