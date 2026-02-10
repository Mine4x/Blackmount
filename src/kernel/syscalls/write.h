#pragma once
#include <stdio.h>
#include <stdint.h>

int32_t sys_write(uint32_t fd, uint32_t buf, uint32_t count, uint32_t unused1, uint32_t unused2) {
    if (buf == 0) { 
        return -1;
    }

    const char* buffer = (const char*)buf;

    if (fd == 1 || fd == 2) {
        printf(buffer);

        return count;
    }

    return -1;
}