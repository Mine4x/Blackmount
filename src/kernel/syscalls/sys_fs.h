#pragma once
#include <stdint.h>
#include <hal/vfs.h>
#include <proc/proc.h>
#include <arch/x86_64/io.h>
#include <console/console.h>

uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                  uint64_t unused1, uint64_t unused2, uint64_t unused3)
{
    (void)unused1;
    (void)unused2;
    (void)unused3;

    return (uint64_t)VFS_Write_old(
        (fd_t)fd,
        (uint8_t*)buf,
        (size_t)count
    );
}

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
    return VFS_Open((const char*)path, false);
}