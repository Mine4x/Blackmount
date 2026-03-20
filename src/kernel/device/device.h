#ifndef DEVICE_H
#define DEVICE_H
#include <stdint.h>

typedef int (*device_dispatch)(
    int pid,
    uint64_t req,
    void *arg
);

typedef struct device
{
    const char* path;

    device_dispatch dispatch;
} device_t;


#endif