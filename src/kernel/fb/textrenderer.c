#include "textrenderer.h"
#include "framebuffer.h"
#include "std_font.h"

#define FONT_WIDTH  8
#define FONT_HEIGHT 8

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

static uint32_t fg_color = 0xFFFFFF;
static uint32_t bg_color = 0x000000;

static uint32_t screen_width;
static uint32_t screen_height;

static void draw_char(uint32_t px, uint32_t py, char c) {
    unsigned char uc = (unsigned char)c;
    if (uc >= 128) uc = '?'; // fallback

    const uint8_t *glyph = std_font[uc];

    for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
        uint8_t row = glyph[y];
        for (uint32_t x = 0; x < FONT_WIDTH; x++) {
            if (row & (0x80 >> x)) {
                fb_putpixel(px + x, py + y, fg_color);
            } else {
                fb_putpixel(px + x, py + y, bg_color);
            }
        }
    }
}

static void newline(void) {
    cursor_x = 0;
    cursor_y++;

    if ((cursor_y + 1) * FONT_HEIGHT >= screen_height) {
        cursor_y = 0;
    }
}

void tr_init(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;

    screen_width  = fb_get_width();
    screen_height = fb_get_height();

    cursor_x = 0;
    cursor_y = 0;
}

void tr_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

void tr_putc(char c) {
    if (c == '\n') {
        newline();
        return;
    }

    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    uint32_t px = cursor_x * FONT_WIDTH;
    uint32_t py = cursor_y * FONT_HEIGHT;

    draw_char(px, py, c);

    cursor_x++;
    if ((cursor_x + 1) * FONT_WIDTH >= screen_width) {
        newline();
    }
}

void tr_write(const char *str) {
    while (*str) {
        tr_putc(*str++);
    }
}
