#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <arch/i686/irq.h>

void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_has_input(void);

#endif