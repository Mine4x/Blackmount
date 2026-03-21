#ifndef CONSOLE_H
#define CONSOLE_H

#include <syscalls.h>
#include <stdint.h>

#define TTY_CLEAR 1
#define TTY_COLOR 2
#define TTY_SPECIAL_READ 3

typedef struct
{
    uint32_t fg;
    uint32_t bg;
} tty_color_t;

typedef struct
{
    size_t count;
    char* buf;
} tty_read_special_request_t;

int console_clear(int fd);
int console_set_color(int fd, tty_color_t *color);
int console_read_special_chars(int fd, tty_read_special_request_t *req);

#endif