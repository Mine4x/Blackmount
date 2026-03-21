#ifndef DEVICE_H
#define DEVICE_H
#include <stdint.h>
#include <stdbool.h>

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

void device_init(void);
bool device_register(device_t* dev);
device_t* device_get(const char* path);
bool device_unregister(const char* path);

#endif