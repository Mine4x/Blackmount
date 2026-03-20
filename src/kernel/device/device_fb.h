#ifndef DEV_FB_H
#define DEV_FB_H

#include <stdint.h>
#include "device.h"

#define FB_GET_INFO 1

typedef struct fb_info
{
    uint32_t width;
    uint32_t height;
    uint32_t pitch; // bytes per row
    uint32_t bpp; // bits per pixel
} fb_info_t;

device_t* fb_device_init(const char* path);

#endif