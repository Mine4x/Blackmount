#include <termio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int tty_open(void)
{
    uint64_t fd = open("/dev/tty");
    if (fd == (uint64_t)-1) {
        errno = ENOTTY;
        return -1;
    }
    return (int)fd;
}

static int tty_fd(int fd)
{
    if (fd >= 0)
        return fd;
    return tty_open();
}

static void tty_close_if_opened(int original_fd, int used_fd)
{
    if (original_fd < 0)
        close((uint64_t)used_fd);
}

int tcgetattr(int fd, struct termios *t)
{
    if (!t) {
        errno = EINVAL;
        return -1;
    }

    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    int ret = (int)ioctl(f, TCGETS, t);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

int tcsetattr(int fd, int action, const struct termios *t)
{
    if (!t) {
        errno = EINVAL;
        return -1;
    }

    uint64_t req;
    switch (action) {
        case TCSANOW:   req = TCSETS;  break;
        case TCSADRAIN: req = TCSETSW; break;
        case TCSAFLUSH: req = TCSETSF; break;
        default:
            errno = EINVAL;
            return -1;
    }

    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    int ret = (int)ioctl(f, req, (void *)t);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

int tcflush(int fd, int queue)
{
    if (queue != TCIFLUSH && queue != TCOFLUSH && queue != TCIOFLUSH) {
        errno = EINVAL;
        return -1;
    }

    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    int ret = (int)ioctl(f, TCFLSH, &queue);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

int tcdrain(int fd)
{
    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    struct termios t;
    int ret = (int)ioctl(f, TCGETS, &t);
    if (ret == 0)
        ret = (int)ioctl(f, TCSETSW, &t);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

int tcflow(int fd, int action)
{
    if (action != TCOOFF && action != TCOON &&
        action != TCIOFF && action != TCION) {
        errno = EINVAL;
        return -1;
    }

    (void)fd;
    return 0;
}

int tcsendbreak(int fd, int duration)
{
    (void)fd;
    (void)duration;
    return 0;
}

pid_t tcgetpgrp(int fd)
{
    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    pid_t pgrp = -1;
    int ret = (int)ioctl(f, TIOCGPGRP, &pgrp);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return pgrp;
}

int tcsetpgrp(int fd, pid_t pgrp)
{
    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    int ret = (int)ioctl(f, TIOCSPGRP, &pgrp);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

int tcgetwinsize(int fd, struct winsize *w)
{
    if (!w) {
        errno = EINVAL;
        return -1;
    }

    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    int ret = (int)ioctl(f, TIOCGWINSZ, w);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

int tcsetwinsize(int fd, const struct winsize *w)
{
    if (!w) {
        errno = EINVAL;
        return -1;
    }

    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    int ret = (int)ioctl(f, TIOCSWINSZ, (void *)w);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

int tcgetoutq(int fd, int *count)
{
    if (!count) {
        errno = EINVAL;
        return -1;
    }

    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    int ret = (int)ioctl(f, TIOCOUTQ, count);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

int tcgetinq(int fd, int *count)
{
    if (!count) {
        errno = EINVAL;
        return -1;
    }

    int f = tty_fd(fd);
    if (f < 0)
        return -1;

    int ret = (int)ioctl(f, FIONREAD, count);

    tty_close_if_opened(fd, f);

    if (ret < 0) {
        errno = ENOTTY;
        return -1;
    }

    return 0;
}

speed_t cfgetospeed(const struct termios *t)
{
    if (!t) {
        errno = EINVAL;
        return B0;
    }
    return (speed_t)(t->c_cflag & CBAUD);
}

speed_t cfgetispeed(const struct termios *t)
{
    if (!t) {
        errno = EINVAL;
        return B0;
    }
    return (speed_t)(t->c_cflag & CBAUD);
}

int cfsetospeed(struct termios *t, speed_t speed)
{
    if (!t || (speed & ~CBAUD)) {
        errno = EINVAL;
        return -1;
    }
    t->c_cflag = (t->c_cflag & ~CBAUD) | (speed & CBAUD);
    return 0;
}

int cfsetispeed(struct termios *t, speed_t speed)
{
    if (!t) {
        errno = EINVAL;
        return -1;
    }
    if (speed == B0)
        return 0;
    if (speed & ~CBAUD) {
        errno = EINVAL;
        return -1;
    }
    t->c_cflag = (t->c_cflag & ~CBAUD) | (speed & CBAUD);
    return 0;
}

int cfsetspeed(struct termios *t, speed_t speed)
{
    if (!t || (speed & ~CBAUD)) {
        errno = EINVAL;
        return -1;
    }
    t->c_cflag = (t->c_cflag & ~CBAUD) | (speed & CBAUD);
    return 0;
}

void cfmakeraw(struct termios *t)
{
    if (!t)
        return;

    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                    INLCR  | IGNCR  | ICRNL  | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |=  CS8;
    t->c_cc[VMIN]  = 1;
    t->c_cc[VTIME] = 0;
}