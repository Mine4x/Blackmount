#pragma once
#include <stdint.h>
#include <hal/vfs.h>

int32_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                  uint64_t unused1, uint64_t unused2)
{
    (void)unused1;
    (void)unused2;

    return VFS_Write(
        (fd_t)fd,
        (uint8_t*)buf,
        (size_t)count
    );
}
