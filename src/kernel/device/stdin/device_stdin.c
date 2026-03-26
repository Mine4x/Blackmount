#include "device_stdin.h"
#include <stddef.h>
#include <heap.h>
#include <console/console.h>
#include <proc/proc.h>
#include <hal/vfs.h>
#include <errno/errno.h>

static int rmc(int pid, void *arg)
{
    console_backspace();

    return 0;
}

static int clear(int pid, void *arg)
{
    console_clear();

    return 0;
}

static int read_c(int pid, void *arg)
{
    if (!arg)
        return -1;

    char c = console_get_current_c();

    char output;

    output = c;

    if (!proc_write_to_user(pid, arg, &output, sizeof(char)))
    {
        return -1;
    }

    return 0;
}

static int dispatcher(int pid, uint64_t req, void *arg)
{
    switch (req)
    {
    case STDIN_RMC:
        return rmc(pid, arg);
    case STDIN_CLEAR:
        return clear(pid, arg);
    case STDIN_READ_C:
        return read_c(pid, arg);
    
    default:
        return -1;
    }
}

static int read(size_t count, void* buf)
{
    int pid = proc_get_current_pid();
    if (pid < 0)
        return serror(ESRCH);

    console_register_proc(pid, (void*)buf, count);

    x86_64_EnableInterrupts();

    proc_yield();

    return count;
}

static int write(size_t count, void* buf)
{
    return -1;
}

device_t* stdin_device_init(const char* path)
{
    if (VFS_Create(path, false) < 0)
        return NULL;

    device_t* dev = kmalloc(sizeof(device_t));

    if (!dev) return NULL;

    dev->path = path;
    dev->dispatch = &dispatcher;
    dev->read = &read;
    dev->write = &write;

    device_register(dev);

    return dev;
}