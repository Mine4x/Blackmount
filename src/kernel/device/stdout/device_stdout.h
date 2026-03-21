#ifndef STDOUT_DEV_H
#define STDOUT_DEV_H

#include <stddef.h>

typedef struct
{
    size_t count;
    char* buf;
} write_event_t;

#define STDOUT_WRITE 1
#define STDOUT_PUTC 2
#define STDOUT_RMC 3

#include <device/device.h>

device_t* stdout_device_init(const char* path);

#endif