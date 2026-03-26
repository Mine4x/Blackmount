#pragma once
#include <stdint.h>
#include <hal/vfs.h>
#include <proc/proc.h>
#include <arch/x86_64/io.h>
#include <console/console.h>
#include <errno/errno.h>

#define TCGETS 0x5401

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   64
#define O_TRUNC   512
#define O_APPEND  1024

uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                  uint64_t unused1, uint64_t unused2, uint64_t unused3)
{
    (void)unused1;
    (void)unused2;
    (void)unused3;

    if (fd != VFS_FD_STDIN) {
        int r = VFS_Read((int)fd, (size_t)count, (void*)buf);
        return r < 0 ? serror(-r) : (uint64_t)r;
    }

    int pid = proc_get_current_pid();
    if (pid < 0)
        return serror(ESRCH);

    console_register_proc(pid, (void*)buf, count);

    x86_64_EnableInterrupts();

    proc_yield();

    return count;
}

uint64_t sys_open(uint64_t path, uint64_t flags)
{
    if (!path)
        return serror(EFAULT);

    if (flags & O_CREAT) {
        int result = VFS_Create((const char*)path, false);
        if (result < 0)
            return serror(EEXIST);
    }

    int fd = VFS_Open((const char*)path, (flags & O_RDWR) || (flags & O_WRONLY));
    if (fd < 0)
        return serror(ENOENT);

    return (uint64_t)fd;
}

uint64_t sys_close(uint64_t fd)
{
    int r = VFS_Close((int)fd, false);
    return r < 0 ? serror(EBADF) : 0;
}

uint64_t sys_ioctl(uint64_t fd, uint64_t req, uint64_t arg)
{
    if ((fd == 1 || fd == 2) && req == TCGETS) {
        if (arg) memset((void *)arg, 0, 44);
        return 0;
    }

    int r = VFS_ioctl((int)fd, req, (void *)arg);
    return r < 0 ? serror(EBADF) : (uint64_t)r;
}

uint64_t sys_getdents64(uint64_t fd, uint64_t buf, uint64_t size)
{
    if (!buf)
        return serror(EFAULT);

    int r = VFS_GetDents64((int)fd, (struct linux_dirent64*)buf, (size_t)size);
    return r < 0 ? serror(EBADF) : (uint64_t)r;
}

uint64_t sys_create(uint64_t path, uint64_t is_dir)
{
    if (!path)
        return serror(EFAULT);

    int r = VFS_Create((const char*)path, (bool)is_dir);
    return r < 0 ? serror(EEXIST) : 0;
}