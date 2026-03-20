#ifndef DEV_TTY_H
#define DEV_TTY_H

#define TTY_CLEAR 1
#define TTY_COLOR 2
#define TTY_SPECIAL_READ 3

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint32_t fg;
    uint32_t bg;
} tty_color_t;

typedef struct
{
    size_t count;
    char* buf;
} read_special_event_t;


#include <device/device.h>
#include <console/console.h>
#include <fb/textrenderer.h>

device_t* tty_device_init(const char* path);

#endif