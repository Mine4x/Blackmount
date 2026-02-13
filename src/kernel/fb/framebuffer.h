#pragma once
#include <stdint.h>
#include <stddef.h>
#include <limine/limine.h>

void fb_init(struct limine_framebuffer_response *response);

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
void fb_clear(uint32_t color);

uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
uint32_t fb_get_pitch(void);
void fb_scroll(uint32_t pixels, uint32_t bg_color);