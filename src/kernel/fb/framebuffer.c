#include "framebuffer.h"

static uint8_t *fb_addr = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint16_t fb_bpp = 0;

void fb_init(struct limine_framebuffer_response *response) {
    struct limine_framebuffer *fb = response->framebuffers[0];

    fb_addr   = (uint8_t*)fb->address;
    fb_width  = fb->width;
    fb_height = fb->height;
    fb_pitch  = fb->pitch;
    fb_bpp    = fb->bpp;
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_height)
        return;

    uint32_t bytes_per_pixel = fb_bpp / 8;
    uint8_t *pixel = fb_addr + y * fb_pitch + x * bytes_per_pixel;

    *(uint32_t*)pixel = color;
}

void fb_clear(uint32_t color) {
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            fb_putpixel(x, y, color);
        }
    }
}

uint32_t fb_get_width(void)  { return fb_width; }
uint32_t fb_get_height(void) { return fb_height; }
uint32_t fb_get_pitch(void)  { return fb_pitch; }
