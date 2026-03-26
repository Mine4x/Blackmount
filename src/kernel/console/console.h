#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <fb/textrenderer.h>
#include <stddef.h>
#include <util/vector.h>
#include <proc/proc.h>
#include <memory.h>
#include <heap.h>
#include <stdio.h>
#include <stdbool.h>
#include <drivers/usb/xhci/usb_hid.h>

#define CONSOLE_BUFFER_SIZE 128
#define CONSOLE_MODULE "Console"

#define NCCS 19

typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;

#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP   10
#define VEOL    11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT  15
#define VEOL2   16

#define IGNBRK  0x0001
#define BRKINT  0x0002
#define IGNPAR  0x0004
#define PARMRK  0x0008
#define INPCK   0x0010
#define ISTRIP  0x0020
#define INLCR   0x0040
#define IGNCR   0x0080
#define ICRNL   0x0100
#define IUCLC   0x0200
#define IXON    0x0400
#define IXANY   0x0800
#define IXOFF   0x1000
#define IMAXBEL 0x2000
#define IUTF8   0x4000

#define OPOST  0x0001
#define OLCUC  0x0002
#define ONLCR  0x0004
#define OCRNL  0x0008
#define ONOCR  0x0010
#define ONLRET 0x0020
#define OFILL  0x0040
#define OFDEL  0x0080

#define CS5    0x0000
#define CS6    0x0010
#define CS7    0x0020
#define CS8    0x0030
#define CSTOPB 0x0040
#define CREAD  0x0080
#define PARENB 0x0100
#define PARODD 0x0200
#define HUPCL  0x0400
#define CLOCAL 0x0800

#define ISIG    0x0001
#define ICANON  0x0002
#define XCASE   0x0004
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080
#define TOSTOP  0x0100
#define ECHOCTL 0x0200
#define ECHOPRT 0x0400
#define ECHOKE  0x0800
#define IEXTEN  0x8000

#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[NCCS];
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} InputManager;

typedef struct
{
    int pid;
    void* buffer;
    size_t buf_size;
    bool active;
} WaitingProc;

void console_putc(char c);

static void handle_escape_sequence(void);
static void draw_cursor(void);

void console_clear_text();
void console_make_dev();

void console_backspace_no_input(void);
char console_get_current_c();
void console_set_current_c(char c);
void console_reset_special_char();
void console_add_special_char(uint64_t sc);
void console_backspace_no_dispaly(void);
void console_read_special_char(char* buf, size_t count);

static bool copy_to_user(void* user_ptr, const void* kernel_src, size_t n);
bool console_init(void);
void console_free(void);
bool console_addchar(char c);
bool console_rmchar(void);
void console_clear(void);
size_t console_get_length(void);
void console_register_proc(int pid, void* buf, size_t buffer_size);
void console_backspace();
void console_user_put_c(char c);
void console_unregister_proc(int pid);

void console_get_termios(struct termios *t);
void console_set_termios(const struct termios *t);
void console_get_winsize(struct winsize *w);
void console_set_winsize(const struct winsize *w);
void console_tcflush(int queue);
int  console_get_pgrp(void);
void console_set_pgrp(int pgrp);

bool console_is_raw_mode(void);

#endif