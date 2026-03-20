#include "device_tty.h"

static int clear(int pid, void *arg)
{
    console_clear_text();
    console_clear();

    return 0;
}

static int set_color(int pid, void *arg)
{
    if (!arg) return -1;

    tty_color_t* color = (tty_color_t*)arg;

    tr_set_color(color->fg, color->bg);

    return 0;
}

static int dispatcher(int pid, uint64_t req, void *arg)
{
    switch (req)
    {
    case TTY_CLEAR:
        return clear(pid, arg);
    case TTY_COLOR:
        return set_color(pid, arg);
    
    default:
        return -1;
    }
}

device_t* tty_device_init(const char* path)
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