#include "framebuffer.h"
#include <stdint.h>
#include <memory.h>

static uint8_t *fb_addr = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint16_t fb_bpp = 0;

void fb_init(struct limine_framebuffer_response *response) {
    if (!response || response->framebuffer_count == 0)
        return;
    struct limine_framebuffer *fb = response->framebuffers[0];
    fb_addr   = (uint8_t*)fb->address;
    fb_width  = fb->width;
    fb_height = fb->height;
    fb_pitch  = fb->pitch;
    fb_bpp    = fb->bpp;
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_addr)
        return;
    if (x >= fb_width || y >= fb_height)
        return;
    uint8_t *pixel = fb_addr + y * fb_pitch + x * (fb_bpp / 8);
    if (fb_bpp == 32)
        *(uint32_t*)pixel = color;
    else if (fb_bpp == 24) {
        pixel[0] = color & 0xFF;
        pixel[1] = (color >> 8) & 0xFF;
        pixel[2] = (color >> 16) & 0xFF;
    }
}

void fb_clear(uint32_t color) {
    if (!fb_addr || fb_bpp != 32)
        return;

    uint32_t *fb32 = (uint32_t*)fb_addr;
    uint32_t pixels_per_row = fb_pitch / 4;

    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            fb32[y * pixels_per_row + x] = color;
        }
    }
}


void fb_scroll(uint32_t pixels, uint32_t bg_color) {
    if (!fb_addr || fb_bpp != 32)
        return;

    uint32_t bytes_per_pixel = fb_bpp / 8;
    uint32_t row_size = fb_pitch;
    uint32_t scroll_bytes = pixels * row_size;

    // Move framebuffer memory up
    memmove(
        fb_addr,
        fb_addr + scroll_bytes,
        (fb_height * row_size) - scroll_bytes
    );

    // Clear bottom area
    uint8_t *bottom = fb_addr + (fb_height * row_size) - scroll_bytes;
    uint32_t *bottom32 = (uint32_t*)bottom;
    uint32_t pixels_to_clear = (scroll_bytes / 4);

    for (uint32_t i = 0; i < pixels_to_clear; i++) {
        bottom32[i] = bg_color;
    }
}

uint32_t fb_get_width(void)  { return fb_width; }
uint32_t fb_get_height(void) { return fb_height; }
uint32_t fb_get_pitch(void)  { return fb_pitch; }
