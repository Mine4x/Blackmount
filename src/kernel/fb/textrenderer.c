#include "textrenderer.h"
#include "framebuffer.h"
#include "font/fontloader.h"
#include <stdint.h>
#include <stdbool.h>

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t fg_color = 0xFFFFFF;
static uint32_t bg_color = 0x000000;
static uint32_t screen_width;
static uint32_t screen_height;

static bool tr_escape_mode = false;
static char tr_escape_buf[16];
static int tr_escape_pos = 0;

static void handle_escape_sequence(void) {
    tr_escape_buf[tr_escape_pos] = 0;
    
    int fg = -1, bg = -1;
    if (tr_escape_pos >= 2 && tr_escape_buf[tr_escape_pos - 1] == 'm') {
        int val = 0;
        int i = 0;
        while (tr_escape_buf[i]) {
            char c = tr_escape_buf[i];
            if (c >= '0' && c <= '9') {
                val = val * 10 + (c - '0');
            } else if (c == ';' || c == 'm') {
                switch (val) {
                    case 0: fg = 0xFFFFFF; bg = 0x000000; break; // reset
                    case 30: fg = 0x000000; break; // black
                    case 31: fg = 0xFF0000; break; // red
                    case 32: fg = 0x00FF00; break; // green
                    case 33: fg = 0xFFFF00; break; // yellow
                    case 34: fg = 0x0000FF; break; // blue
                    case 35: fg = 0xFF00FF; break; // magenta
                    case 36: fg = 0x00FFFF; break; // cyan
                    case 37: fg = 0xFFFFFF; break; // white
                    case 40: bg = 0x000000; break; // black bg
                    case 41: bg = 0xFF0000; break; // red bg
                    case 42: bg = 0x00FF00; break; // green bg
                    case 43: bg = 0xFFFF00; break; // yellow bg
                    case 44: bg = 0x0000FF; break; // blue bg
                    case 45: bg = 0xFF00FF; break; // magenta bg
                    case 46: bg = 0x00FFFF; break; // cyan bg
                    case 47: bg = 0xFFFFFF; break; // white bg
                    default: break;
                }
                val = 0;
            }
            i++;
        }
    }
    
    if (fg != -1 || bg != -1)
        tr_set_color(fg != -1 ? fg : fg_color, bg != -1 ? bg : bg_color);
    
    tr_escape_mode = false;
    tr_escape_pos = 0;
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
    if ((cursor_y + 1) * font->height >= screen_height) {
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
    if (tr_escape_mode) {
        if (tr_escape_pos < 15) {
            tr_escape_buf[tr_escape_pos++] = c;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            handle_escape_sequence();
        }
        return;
    }
    
    if (c == '\x1b') {
        tr_escape_mode = true;
        tr_escape_pos = 0;
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