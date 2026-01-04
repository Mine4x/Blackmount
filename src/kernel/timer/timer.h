#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

extern volatile uint32_t g_pit_ticks;

void timer_init();
uint32_t timer_get_ticks();

#endif