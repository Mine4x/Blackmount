#include <block/block.h>
#include <heap.h>
#include <string.h>
#include <debug.h>

#define MAX_BLOCK_DEVICES 16

static block_device_t* devices[MAX_BLOCK_DEVICES];

bool block_register(block_device_t* dev) {
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (!devices[i]) {
            devices[i] = dev;
            log_ok("BLOCK", "Registered device %s", dev->name);
            return true;
        }
    }
    log_err("BLOCK", "Device table full");
    return false;
}

block_device_t* block_get(const char* name) {
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (devices[i] && strcmp(devices[i]->name, name) == 0)
            return devices[i];
    }
    return NULL;
}
