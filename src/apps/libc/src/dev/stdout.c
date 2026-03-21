#include <dev/stdout.h>

int stdout_write(int fd, stdout_write_request_t* req)
{
    if (!fd || !req)
        return -1;

    return ioctl(fd, STDOUT_WRITE, req);
}

int stdout_putc(int fd, char* c)
{
    if (!fd || !c)
        return -1;

    return ioctl(fd, STDOUT_PUTC, c);
}

int stdout_rmc(int fd)
{
    if (!fd)
        return -1;
    
    return ioctl(fd, STDOUT_RMC, NULL);
}