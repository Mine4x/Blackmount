#ifndef DEV_TTY_H
#define DEV_TTY_H

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TCFLSH      0x540B
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410
#define TIOCOUTQ    0x5411
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define FIONREAD    0x541B

#include <stdint.h>
#include <stddef.h>
#include <device/device.h>
#include <console/console.h>
#include <fb/textrenderer.h>

device_t* tty_device_init(const char* path);

#endif