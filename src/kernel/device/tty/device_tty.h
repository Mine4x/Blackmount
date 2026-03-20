#ifndef DEV_TTY_H
#define DEV_TTY_H

#define TTY_CLEAR 1
#define TTY_COLOR 2

#include <stdint.h>

typedef struct
{
    uint32_t fg;
    uint32_t bg;
} tty_color_t;


#include <device/device.h>
#include <console/console.h>
#include <fb/textrenderer.h>

device_t* tty_device_init(const char* path);

#endif