#include <syscalls.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct fb_info
{
    uint32_t width;
    uint32_t height;
    uint32_t pitch; // bytes per row
    uint32_t bpp; // bits per pixel
} fb_info_t;

int main()
{
    int fd = open("/dev/fb");
    if (fd < 0)
    {
        printf("Unable to open device\n");
    }

    fb_info_t* info = malloc(sizeof(fb_info_t));

    syscall6(16, fd, 1, (uint64_t)info, 0, 0, 0);

    printf("Got fb_info:\n  width=%d\n  height=%d\n  pitch=%d\n  bpp=%d\n", info->width, info->height, info->pitch, info->bpp);

    return 0;
}