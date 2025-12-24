#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <arch/i686/irq.h>

void ps2_keyboard_init(void);
char ps2_keyboard_getchar(void);
bool ps2_keyboard_has_input(void);

#endif