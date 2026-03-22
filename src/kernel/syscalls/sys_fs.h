#pragma once
#include <stdint.h>
#include <hal/vfs.h>
#include <proc/proc.h>
#include <arch/x86_64/io.h>
#include <console/console.h>

#define TCGETS 0x5401

uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                  uint64_t unused1, uint64_t unused2, uint64_t unused3)
{
    (void)unused1;
    (void)unused2;
    (void)unused3;

    if (fd != VFS_FD_STDIN)
        return (uint64_t)VFS_Read((int)fd, (size_t)count, (void*)buf);

    int pid = proc_get_current_pid();
    console_register_proc(pid, (void*)buf, count);

    x86_64_EnableInterrupts();

    proc_yield();
    

    return count;
}

uint64_t sys_open(uint64_t path)
{
    return (uint64_t)VFS_Open((const char*)path, false);
}

uint64_t sys_close(uint64_t fd)
{
    return (uint64_t)VFS_Close(fd, false);
}

uint64_t sys_ioctl(uint64_t fd, uint64_t req, uint64_t arg)
{
    if ((fd == 1 || fd == 2) && req == TCGETS) {
        if (arg) memset((void *)arg, 0, 44);
        return 0;
    }

    return VFS_ioctl((int)fd, req, (void *)arg);
}

uint64_t sys_getdents64(uint64_t fd, uint64_t buf, uint64_t size)
{
    return VFS_GetDents64(fd, (struct linux_dirent64*)buf, (size_t)size);
}

uint64_t sys_create(uint64_t path, uint64_t is_dir)
{
    return VFS_Create((const char*)path, is_dir);
}