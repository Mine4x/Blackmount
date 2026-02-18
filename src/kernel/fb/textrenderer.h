#pragma once
#include <stdint.h>

void tr_init(uint32_t fg, uint32_t bg);
void tr_putc(char c);
void tr_write(const char *str);
void tr_set_color(uint32_t fg, uint32_t bg);
void tr_backspace(void);