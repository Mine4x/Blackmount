#ifndef FB_H
#define FB_H

#include <syscalls.h>

#define FB_GET_INFO 1

typedef struct fb_info
{
    uint32_t width;
    uint32_t height;
    uint32_t pitch; // bytes per row
    uint32_t bpp; // bits per pixel
} fb_info_t;

int fb_get_info(int fd, fb_info_t *info);

#endif