#ifndef STDIN_DEV_H
#define STDIN_DEV_H

#include <stddef.h>

#define STDIN_RMC 1
#define STDIN_CLEAR 2
#define STDIN_READ_C 3

#include <device/device.h>

device_t* stdin_device_init(const char* path);

#endif