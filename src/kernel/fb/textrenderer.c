#include "textrenderer.h"
#include "framebuffer.h"
#include "font/fontloader.h"
#include <stdint.h>
#include <debug.h>
#include <stdbool.h>

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t fg_color = 0xFFFFFF;
static uint32_t bg_color = 0x000000;
static uint32_t screen_width;
static uint32_t screen_height;

static bool started = false;

void tr_clear()
{
    cursor_x = 0;
    cursor_y = 0;
    fb_clear(bg_color);
}

static void draw_char(uint32_t px, uint32_t py, char c) {
    unsigned char uc = (unsigned char)c;
    const font_t *font = font_get_current();
    const uint8_t *glyph = font_get_glyph(uc);
    
    uint8_t font_width = font->width;
    uint8_t font_height = font->height;
    
    for (uint32_t y = 0; y < font_height; y++) {
        uint8_t row = glyph[y];
        for (uint32_t x = 0; x < font_width; x++) {
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

    const font_t *font = font_get_current();
    uint32_t font_height = font->height;

    if ((cursor_y + 1) * font_height >= screen_height) {
        // Scroll framebuffer up by one text row
        fb_scroll(font_height, bg_color);

        // Keep cursor on last line
        cursor_y--;
    }
}

void tr_init(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
    screen_width  = fb_get_width();
    screen_height = fb_get_height();
    cursor_x = 0;
    cursor_y = 0;
    started = true;
}

void tr_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

void tr_putc(char c) {
    if (!started) {
        return;
    }
    
    if (c == '\n') {
        newline();
        return;
    }
    
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    
    const font_t *font = font_get_current();
    uint32_t px = cursor_x * font->width;
    uint32_t py = cursor_y * font->height;
    
    draw_char(px, py, c);
    cursor_x++;
    
    if ((cursor_x + 1) * font->width >= screen_width) {
        newline();
    }
}

void tr_write(const char *str) {
    while (*str) {
        tr_putc(*str++);
    }
}

void tr_backspace(void) {
    if (!started)
        return;

    const font_t *font = font_get_current();
    uint32_t font_width = font->width;
    uint32_t font_height = font->height;

    // If cursor is at the very beginning, nothing to do
    if (cursor_x == 0 && cursor_y == 0)
        return;

    // Move cursor back
    if (cursor_x == 0) {
        // Move to end of previous line
        cursor_y--;
        cursor_x = screen_width / font_width - 1;
    } else {
        cursor_x--;
    }

    // Calculate pixel position
    uint32_t px = cursor_x * font_width;
    uint32_t py = cursor_y * font_height;

    // Draw blank rectangle to erase character
    for (uint32_t y = 0; y < font_height; y++) {
        for (uint32_t x = 0; x < font_width; x++) {
            fb_putpixel(px + x, py + y, bg_color);
        }
    }
}
