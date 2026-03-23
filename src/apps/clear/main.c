#include <stdio.h>
#include <unistd.h>
#include <dev/console.h>

#define TTY_CLEAR 1

int main()
{
    int fd = open("/dev/tty");
    if (fd < 0)
    {
        printf("Unable to open /dev/tty\n");
        //printf("\033[2J");
    }

    //ioctl(fd, TTY_CLEAR, NULL);
    console_clear(fd);

    close(fd);

    return 0;
}