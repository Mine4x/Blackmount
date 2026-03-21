#include <dev/console.h>

int console_clear(int fd)
{
    if (!fd)
        return -1;

    return ioctl(fd, TTY_CLEAR, NULL);
}

int console_set_color(int fd, tty_color_t *color)
{
    if (!fd || !color)
        return -1;

    return ioctl(fd, TTY_COLOR, (void*)color);
}

int console_read_special_chars(int fd, tty_read_special_request_t *req)
{
    if (!fd || !req)
        return -1;
    
    return ioctl(fd, TTY_SPECIAL_READ, (void*)req);
}
