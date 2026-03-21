#include "device_fb.h"
#include <fb/framebuffer.h>
#include <heap.h>
#include <proc/proc.h>
#include <hal/vfs.h>
#include <debug.h>

static int get_info(int pid, void* arg)
{
    if (!arg) return -1;

    fb_info_t info;

    info.bpp    = fb_get_bpp();
    info.height = fb_get_height();
    info.pitch  = fb_get_pitch();
    info.width  = fb_get_width();

    if (pid < 0) // Only when calling form kernel
    {
        *(fb_info_t*)arg = info;
    }
    else
    {
        bool res = proc_write_to_user(proc_get_current_pid(), arg, &info, sizeof(fb_info_t));
        if (!res)
        {
            return -1;
        }
    }

    return 0;
}

static int dispatcher(int pid, uint64_t req, void *arg)
{
    switch (req)
    {
    case FB_GET_INFO:
        return get_info(pid, arg);
    
    default:
        return -1;
    }
}

device_t* fb_device_init(const char* path)
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