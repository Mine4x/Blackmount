#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define SYS_open  2
#define SYS_close 3
#define SYS_fcntl 72

uint64_t open(const char *path)
{
    int64_t ret = (int64_t)syscall6(SYS_open,
                                    (uint64_t)(uintptr_t)path,
                                    0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return (uint64_t)-1;
    }

    return (uint64_t)ret;
}

uint64_t close(uint64_t fd)
{
    int64_t ret = (int64_t)syscall6(SYS_close,
                                    fd,
                                    0, 0, 0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return (uint64_t)-1;
    }

    return 0;
}

int fcntl(uint64_t fd, int cmd, uint64_t arg)
{
    int64_t ret = (int64_t)syscall6(SYS_fcntl,
                                    fd,
                                    (uint64_t)cmd,
                                    arg,
                                    0, 0, 0);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }

    return (int)ret;
}