#include "device_tty.h"

static int tty_tcgets(int pid, void *arg)
{
    if (!arg) return -1;
    struct termios t;
    console_get_termios(&t);
    if (!proc_write_to_user(pid, arg, &t, sizeof(struct termios)))
        return -1;
    return 0;
}

static int tty_tcsets(int pid, void *arg, int mode)
{
    if (!arg) return -1;
    if (mode == 2)
        console_tcflush(TCIFLUSH);
    console_set_termios((const struct termios *)arg);
    return 0;
}

static int tty_tiocgwinsz(int pid, void *arg)
{
    if (!arg) return -1;
    struct winsize w;
    console_get_winsize(&w);
    if (!proc_write_to_user(pid, arg, &w, sizeof(struct winsize)))
        return -1;
    return 0;
}

static int tty_tiocswinsz(int pid, void *arg)
{
    if (!arg) return -1;
    console_set_winsize((const struct winsize *)arg);
    return 0;
}

static int tty_tcflsh(int pid, void *arg)
{
    int queue = arg ? *(int *)arg : TCIOFLUSH;
    console_tcflush(queue);
    return 0;
}

static int tty_tiocgpgrp(int pid, void *arg)
{
    if (!arg) return -1;
    int pgrp = console_get_pgrp();
    if (!proc_write_to_user(pid, arg, &pgrp, sizeof(int)))
        return -1;
    return 0;
}

static int tty_tiocspgrp(int pid, void *arg)
{
    if (!arg) return -1;
    console_set_pgrp(*(int *)arg);
    return 0;
}

static int tty_tiocoutq(int pid, void *arg)
{
    if (!arg) return -1;
    int pending = 0;
    if (!proc_write_to_user(pid, arg, &pending, sizeof(int)))
        return -1;
    return 0;
}

static int tty_fionread(int pid, void *arg)
{
    if (!arg) return -1;
    int avail = (int)console_get_length();
    if (!proc_write_to_user(pid, arg, &avail, sizeof(int)))
        return -1;
    return 0;
}

static int dispatcher(int pid, uint64_t req, void *arg)
{
    switch (req)
    {
    case TCGETS:     return tty_tcgets(pid, arg);
    case TCSETS:     return tty_tcsets(pid, arg, 0);
    case TCSETSW:    return tty_tcsets(pid, arg, 1);
    case TCSETSF:    return tty_tcsets(pid, arg, 2);
    case TCFLSH:     return tty_tcflsh(pid, arg);
    case TIOCGWINSZ: return tty_tiocgwinsz(pid, arg);
    case TIOCSWINSZ: return tty_tiocswinsz(pid, arg);
    case TIOCGPGRP:  return tty_tiocgpgrp(pid, arg);
    case TIOCSPGRP:  return tty_tiocspgrp(pid, arg);
    case TIOCOUTQ:   return tty_tiocoutq(pid, arg);
    case FIONREAD:   return tty_fionread(pid, arg);
    default:         return -1;
    }
}

device_t* tty_device_init(const char* path)
{
    if (VFS_Create(path, false) < 0)
        return NULL;

    device_t* dev = kmalloc(sizeof(device_t));
    if (!dev) return NULL;

    dev->path     = path;
    dev->dispatch = &dispatcher;

    device_register(dev);
    return dev;
}