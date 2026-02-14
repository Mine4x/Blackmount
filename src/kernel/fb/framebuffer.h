#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <limine/limine_req.h>

// Initialize framebuffer
void fb_init(struct limine_framebuffer_response *response);

// Put a pixel at (x, y) with the given color
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);

// Clear the entire framebuffer with a color
void fb_clear(uint32_t color);

// Scroll the framebuffer up by N pixels
void fb_scroll(uint32_t pixels, uint32_t bg_color);

// Get framebuffer dimensions and properties
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
uint32_t fb_get_pitch(void);

#endif // FRAMEBUFFER_H