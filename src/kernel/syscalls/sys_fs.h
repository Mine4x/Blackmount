#pragma once
#include <stdint.h>
#include <hal/vfs.h>

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

uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count, uint64_t unused1, uint64_t unused2, uint64_t unused3)
{
    (void)unused1;
    (void)unused2;
    (void)unused3;

    return (uint64_t)VFS_Read((int)fd, (size_t)count, (void*)buf);
}