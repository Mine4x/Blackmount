#include <dev/stdin.h>

int stdin_rmc(int fd)
{
    if (!fd)
        return -1;
    
    return ioctl(fd, STDIN_RMC, NULL);
}

int stdin_clear(int fd)
{
    if (!fd)
        return -1;
    
    return ioctl(fd, STDIN_CLEAR, NULL);
}

int stdin_read_c(int fd, char *output)
{
    if (!fd || !output)
        return -1;
    
    return ioctl(fd, STDIN_READ_C, output);
}