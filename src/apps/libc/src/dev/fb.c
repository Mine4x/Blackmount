#include <dev/fb.h>

int fb_get_info(int fd, fb_info_t *info)
{
    if (!fd || !info)
        return -1;

    return ioctl(fd, FB_GET_INFO, info);
}