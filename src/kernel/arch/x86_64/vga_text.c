// BROKEN

#include <stdio.h>
#include "io.h"

#include <stdarg.h>
#include <stdbool.h>

const unsigned SCREEN_WIDTH = 80;//80
const unsigned SCREEN_HEIGHT = 25;//25
const uint8_t DEFAULT_COLOR = 0x7;

uint8_t* g_ScreenBuffer = (uint8_t*)0xB8000;
int g_ScreenX = 0, g_ScreenY = 0;
uint8_t g_CurrentColor = DEFAULT_COLOR;

enum {
    ESC_NONE,
    ESC_BRACKET,
    ESC_NUMBER
} g_EscapeState = ESC_NONE;

int g_EscapeArgs[8];
int g_EscapeArgCount = 0;
int g_CurrentArg = 0;

uint8_t ansi_to_vga(int fg, int bg, bool bright)
{
    static const uint8_t ansi_colors[] = {0, 4, 2, 6, 1, 5, 3, 7};
    uint8_t vga_fg = (fg >= 0 && fg < 8) ? ansi_colors[fg] : 7;
    uint8_t vga_bg = (bg >= 0 && bg < 8) ? ansi_colors[bg] : 0;
    if (bright && fg >= 0)
        vga_fg |= 8;
    return (vga_bg << 4) | vga_fg;
}

void VGA_putchr(int x, int y, char c)
{
    g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)] = c;
}

void VGA_putcolor(int x, int y, uint8_t color)
{
    g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1] = color;
}

char VGA_getchr(int x, int y)
{
    return g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)];
}

uint8_t VGA_getcolor(int x, int y)
{
    return g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x) + 1];
}

void VGA_setcursor(int x, int y)
{
    int pos = y * SCREEN_WIDTH + x;

    x86_64_outb(0x3D4, 0x0F);
    x86_64_outb(0x3D5, (uint8_t)(pos & 0xFF));
    x86_64_outb(0x3D4, 0x0E);
    x86_64_outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void VGA_clrscr()
{
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_putchr(x, y, '\0');
            VGA_putcolor(x, y, DEFAULT_COLOR);
        }

    g_ScreenX = 0;
    g_ScreenY = 0;
    g_CurrentColor = DEFAULT_COLOR;
    VGA_setcursor(g_ScreenX, g_ScreenY);
}

void VGA_scrollback(int lines)
{
    for (int y = lines; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_putchr(x, y - lines, VGA_getchr(x, y));
            VGA_putcolor(x, y - lines, VGA_getcolor(x, y));
        }
    
    for (int y = SCREEN_HEIGHT - lines; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            VGA_putchr(x, y, '\0');
            VGA_putcolor(x, y, DEFAULT_COLOR);
        }
    
    g_ScreenY -= lines;
}

void process_escape_sequence()
{
    if (g_EscapeArgCount == 0)
        return;
    
    int fg = g_CurrentColor & 0x0F;
    int bg = (g_CurrentColor >> 4) & 0x0F;
    bool bright = (fg & 8) != 0;
    fg &= 7;
    bg &= 7;
    
    for (int i = 0; i < g_EscapeArgCount; i++)
    {
        int arg = g_EscapeArgs[i];
        if (arg == 0)
        {
            fg = 7;
            bg = 0;
            bright = false;
        }
        else if (arg == 1)
            bright = true;
        else if (arg == 22)
            bright = false;
        else if (arg >= 30 && arg <= 37)
            fg = arg - 30;
        else if (arg >= 40 && arg <= 47)
            bg = arg - 40;
        else if (arg >= 90 && arg <= 97)
        {
            fg = arg - 90;
            bright = true;
        }
        else if (arg >= 100 && arg <= 107)
            bg = arg - 100;
    }
    
    g_CurrentColor = ansi_to_vga(fg, bg, bright);
}

void VGA_putc(char c)
{
    if (g_EscapeState == ESC_NONE && c == '\x1B')
    {
        g_EscapeState = ESC_BRACKET;
        return;
    }
    
    if (g_EscapeState == ESC_BRACKET)
    {
        if (c == '[')
        {
            g_EscapeState = ESC_NUMBER;
            g_EscapeArgCount = 0;
            g_CurrentArg = 0;
            for (int i = 0; i < 8; i++)
                g_EscapeArgs[i] = 0;
            return;
        }
        g_EscapeState = ESC_NONE;
    }
    
    if (g_EscapeState == ESC_NUMBER)
    {
        if (c >= '0' && c <= '9')
        {
            g_CurrentArg = g_CurrentArg * 10 + (c - '0');
            return;
        }
        else if (c == ';')
        {
            if (g_EscapeArgCount < 8)
                g_EscapeArgs[g_EscapeArgCount++] = g_CurrentArg;
            g_CurrentArg = 0;
            return;
        }
        else if (c == 'm')
        {
            if (g_EscapeArgCount < 8)
                g_EscapeArgs[g_EscapeArgCount++] = g_CurrentArg;
            process_escape_sequence();
            g_EscapeState = ESC_NONE;
            return;
        }
        g_EscapeState = ESC_NONE;
        return;
    }
    
    switch (c)
    {
        case '\n':
            g_ScreenX = 0;
            g_ScreenY++;
            break;

        case '\t':
            for (int i = 0; i < 4 - (g_ScreenX % 4); i++)
                VGA_putc(' ');
            break;

        case '\r':
            g_ScreenX = 0;
            break;

        default:
            VGA_putchr(g_ScreenX, g_ScreenY, c);
            VGA_putcolor(g_ScreenX, g_ScreenY, g_CurrentColor);
            g_ScreenX++;
            break;
    }
    
    if (g_ScreenX >= SCREEN_WIDTH)
    {
        g_ScreenY++;
        g_ScreenX = 0;
    }
    if (g_ScreenY >= SCREEN_HEIGHT)
        VGA_scrollback(1);

    VGA_setcursor(g_ScreenX, g_ScreenY);
}

void VGA_backspace()
{
    if (g_ScreenX == 0 && g_ScreenY == 0)
        return;

    if (g_ScreenX == 0) {
        g_ScreenY--;
        g_ScreenX = SCREEN_WIDTH - 1;
    } else {
        g_ScreenX--;
    }

    VGA_putchr(g_ScreenX, g_ScreenY, '\0');
    VGA_putcolor(g_ScreenX, g_ScreenY, g_CurrentColor);

    VGA_setcursor(g_ScreenX, g_ScreenY);
}
