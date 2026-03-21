#include "device_stdout.h"
#include <stddef.h>
#include <heap.h>
#include <console/console.h>

static int write(int pid, void *arg)
{
    write_event_t* event = (write_event_t*)arg;

    for (size_t i = 0; i < event->count; i++)
    {
        console_putc(event->buf[i]);
    }

    return 0;
}

static int _putc(int pid, void *arg)
{
    console_putc((char*)arg);

    return 0;
}

static int rmc(int pid, void *arg)
{
    console_backspace_no_input();

    return 0;
}

static int dispatcher(int pid, uint64_t req, void *arg)
{
    switch (req)
    {
    case STDOUT_WRITE:
        return write(pid, arg);
    case STDOUT_PUTC:
        return _putc(pid, arg);
    case STDOUT_RMC:
        return rmc(pid, arg);
    
    default:
        return -1;
    }
}

device_t* stdout_device_init(const char* path)
{
    if (VFS_Create(path, false) < 0)
        return NULL;

    device_t* dev = kmalloc(sizeof(device_t));

    if (!dev) return NULL;

    dev->path = path;
    dev->dispatch = &dispatcher;

    device_register(dev);

    return dev;
}