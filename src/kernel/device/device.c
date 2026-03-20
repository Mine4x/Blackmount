#include "device.h"
#include <heap.h>
#include <string.h>
#include <hal/vfs.h>
#include <debug.h>

#define MAX_DEVICES 32

static device_t** devices = NULL;

void device_init(void)
{
    devices = kmalloc(MAX_DEVICES * sizeof(device_t*));
    for (int i = 0; i < MAX_DEVICES; i++)
        devices[i] = NULL;
}

bool device_register(device_t* dev)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i]) {
            devices[i] = dev;

            VFS_Create(dev->path, false);
            
            log_ok("DEVICE", "Registered device %s", dev->path);
            return true;
        }
    }
    log_err("DEVICE", "Device table full");
    return false;
}

device_t* device_get(const char* path)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i] && strcmp(devices[i]->path, path) == 0)
            return devices[i];
    }
    return NULL;
}

bool device_unregister(const char* path)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i] && strcmp(devices[i]->path, path) == 0) {
            log_ok("DEVICE", "Unregistered device %s", devices[i]->path);
            devices[i] = NULL;
            return true;
        }
    }
    log_err("DEVICE", "Device %s not found", path);
    return false;
}