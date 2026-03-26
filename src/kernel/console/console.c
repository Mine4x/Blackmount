#include "console.h"
#include <proc/proc.h>
#include <debug.h>
#include <device/tty/device_tty.h>
#include <util/rgb.h>

static InputManager *input = NULL;

vector waitingProcesses;

static uint32_t fg_color = 0xFFFFFF;
static uint32_t bg_color = 0x000000;

static char c_current_key;

static bool c_escape_mode = false;
static char c_escape_buf[26];
static int  c_escape_pos  = 0;

static uint64_t c_special_buf[26];
static int c_special_buf_pos = 0;

static struct termios c_termios = {
    .c_iflag = ICRNL | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = CS8 | CREAD | CLOCAL,
    .c_lflag = ICANON | ECHO | ECHOE | ECHOK | ISIG | IEXTEN,
    .c_line  = 0,
    .c_cc    = {
        [VINTR]  = 3,
        [VQUIT]  = 28,
        [VERASE] = 127,
        [VKILL]  = 21,
        [VEOF]   = 4,
        [VTIME]  = 0,
        [VMIN]   = 1,
        [VSWTC]  = 0,
        [VSTART] = 17,
        [VSTOP]  = 19,
        [VSUSP]  = 26,
        [VEOL]   = 0,
    },
};

static struct winsize c_winsize = {
    .ws_row    = 25,
    .ws_col    = 80,
    .ws_xpixel = 0,
    .ws_ypixel = 0,
};

static int c_pgrp = 0;

void console_add_special_char(uint64_t sc)
{
    if (c_special_buf_pos >= 26)
        return;

    c_special_buf[c_special_buf_pos] = sc;
    c_special_buf_pos++;
}

void console_reset_special_char()
{
    c_special_buf_pos = 0;

    for (int i = 0; i < 26; i++)
    {
        c_special_buf[i] = 0;
    }
}

void console_read_special_char(char* buf, size_t count)
{
    for (int i = 0; i < 26 && (size_t)i < count; i++)
    {
        buf[i] = (char)(c_special_buf[i] & 0xFF);
    }
}

void console_set_current_c(char c)
{
    c_current_key = c;

    if (console_is_raw_mode())
    {
        for (int i = 0; i < waitingProcesses.size; i++) {
            WaitingProc **wp_ptr = vector_get(&waitingProcesses, i);
            WaitingProc  *wp     = *wp_ptr;

            if (!wp)
                continue;

            if (wp->buffer && wp->active && wp->buf_size > 0) {
                char tmp[2];
                tmp[0] = c_current_key;
                tmp[1] = '\0';

                if (!proc_write_to_user(wp->pid, wp->buffer,
                                        tmp, 1)) {
                    log_err(CONSOLE_MODULE,
                            "console_raw: proc_write_to_user failed for pid %d",
                            wp->pid);
                }
            }

            proc_unblock(wp->pid);
            kfree(wp);
        }

        vector_clear(&waitingProcesses);
    }
}

char console_get_current_c()
{
    return c_current_key;
}

void console_clear_text()
{
    tr_clear();
    draw_cursor();
}

void console_make_dev()
{
    if (tty_device_init("/dev/tty") == NULL)
    {
        log_warn("Console", "Unable to create device");
    }
}

void console_putc(char c)
{
    if (c_escape_mode) {
        if (c_escape_pos < 15)
            c_escape_buf[c_escape_pos++] = c;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            handle_escape_sequence();
        return;
    }

    if (c == '\x1b') {
        c_escape_mode = true;
        c_escape_pos  = 0;
        return;
    }

    tr_backspace();
    tr_putc(c);
    draw_cursor();
}

static void draw_cursor(void)
{
    tr_putc('_');
}

static void handle_escape_sequence(void)
{
    c_escape_buf[c_escape_pos] = 0;

    if (c_escape_pos >= 2 && c_escape_buf[0] == '[') {
        if (c_escape_pos == 3 && c_escape_buf[1] == '2' && c_escape_buf[2] == 'J') {
            console_clear_text();
            c_escape_mode = false;
            c_escape_pos  = 0;
            return;
        }
    }

    int fg = -1, bg = -1;
    if (c_escape_pos >= 2 && c_escape_buf[c_escape_pos - 1] == 'm') {
        int val = 0;
        int i   = 0;
        while (c_escape_buf[i]) {
            char ch = c_escape_buf[i];
            if (ch >= '0' && ch <= '9') {
                val = val * 10 + (ch - '0');
            } else if (ch == ';' || ch == 'm') {
                switch (val) {
                    case  0: fg = rgb(255,255,255); bg = rgb(0,0,0); break;
                    case 30: fg = rgb(85,85,85); break;
                    case 31: fg = rgb(255,85,85); break;
                    case 32: fg = rgb(85,225,85); break;
                    case 33: fg = rgb(255,255,85); break;
                    case 34: fg = rgb(85,85,255); break;
                    case 35: fg = rgb(255,85,255); break;
                    case 36: fg = rgb(85,255,255); break;
                    case 37: fg = rgb(255,255,255); break;
                    case 40: bg = rgb(0,0,0); break;
                    case 41: bg = rgb(170,0,0); break;
                    case 42: bg = rgb(0,170,0); break;
                    case 43: bg = rgb(170,85,0); break;
                    case 44: bg = rgb(0,0,170); break;
                    case 45: bg = rgb(170,0,170); break;
                    case 46: bg = rgb(0,170,170); break;
                    case 47: bg = rgb(170,170,170); break;
                    default: break;
                }
                val = 0;
            }
            i++;
        }
    }

    if (fg != -1 || bg != -1)
        tr_set_color(fg != -1 ? fg : fg_color, bg != -1 ? bg : bg_color);

    c_escape_mode = false;
    c_escape_pos  = 0;
}

void console_register_proc(int pid, void *buf, size_t buf_size)
{
    WaitingProc *wp = kmalloc(sizeof(WaitingProc));

    wp->pid      = pid;
    wp->buffer   = buf;
    wp->buf_size = buf_size;
    wp->active   = true;

    vector_push(&waitingProcesses, &wp);

    proc_block(pid);
}

void console_unregister_proc(int pid)
{
    for (int i = 0; i < waitingProcesses.size; i++) {
        WaitingProc *proc = (WaitingProc *)vector_get(&waitingProcesses, i);
        if (proc->pid == pid) {
            proc->active = false;
            return;
        }
    }
}

void console_user_put_c(char c)
{
    if (!console_addchar(c)) {
        log_err(CONSOLE_MODULE, "Unable to add char to buffer");
        return;
    }
    tr_backspace();
    tr_putc(c);
    draw_cursor();
}

void console_backspace(void)
{
    if (!console_rmchar()) {
        log_err(CONSOLE_MODULE, "Unable to remove char from buffer");
        return;
    }
    tr_backspace();
    tr_backspace();
    draw_cursor();
}

void console_backspace_no_input(void)
{
    tr_backspace();
    tr_backspace();
    draw_cursor();
}

void console_backspace_no_dispaly(void)
{
    console_rmchar();
}

bool console_init(void)
{
    if (input != NULL)
        return false;

    input = kmalloc(sizeof(InputManager));
    if (!input)
        return false;

    input->buffer = kmalloc(CONSOLE_BUFFER_SIZE);
    if (!input->buffer) {
        kfree(input);
        input = NULL;
        return false;
    }

    input->length   = 0;
    input->capacity = CONSOLE_BUFFER_SIZE;

    vector_init(&waitingProcesses, sizeof(WaitingProc *));
    return true;
}

void console_free(void)
{
    if (!input)
        return;

    console_clear();

    kfree(input->buffer);
    kfree(input);
    input = NULL;
}

bool console_addchar(char c)
{
    if (!input || input->length >= input->capacity)
        return false;

    input->buffer[input->length++] = c;
    return true;
}

bool console_rmchar(void)
{
    if (!input || input->length == 0)
        return false;

    input->length--;
    return true;
}

void console_clear(void)
{
    if (!input)
        return;

    for (int i = 0; i < waitingProcesses.size; i++) {
        WaitingProc **wp_ptr = vector_get(&waitingProcesses, i);
        WaitingProc  *wp     = *wp_ptr;

        if (!wp)
            continue;

        if (wp->buffer && wp->active) {
            size_t copy_len = (input->length < wp->buf_size - 1)
                                ? input->length
                                : wp->buf_size - 1;

            input->buffer[copy_len] = '\0';

            if (!proc_write_to_user(wp->pid, wp->buffer,
                                    input->buffer, copy_len + 1)) {
                log_err(CONSOLE_MODULE,
                        "console_clear: proc_write_to_user failed for pid %d",
                        wp->pid);
            }
        }

        proc_unblock(wp->pid);
        kfree(wp);
    }

    vector_clear(&waitingProcesses);
    input->length = 0;
}

size_t console_get_length(void)
{
    if (!input)
        return (size_t)-1;

    return input->length;
}

void console_get_termios(struct termios *t)
{
    if (!t)
        return;
    *t = c_termios;
}

void console_set_termios(const struct termios *t)
{
    if (!t)
        return;
    c_termios = *t;
}

void console_get_winsize(struct winsize *w)
{
    if (!w)
        return;
    *w = c_winsize;
}

void console_set_winsize(const struct winsize *w)
{
    if (!w)
        return;
    c_winsize = *w;
}

void console_tcflush(int queue)
{
    if (!input)
        return;

    if (queue == TCIFLUSH || queue == TCIOFLUSH)
        input->length = 0;
}

int console_get_pgrp(void)
{
    return c_pgrp;
}

void console_set_pgrp(int pgrp)
{
    c_pgrp = pgrp;
}

bool console_is_raw_mode(void)
{
    return !(c_termios.c_lflag & ICANON) &&
           !(c_termios.c_lflag & ECHO)   &&
           !(c_termios.c_lflag & ISIG)   &&
           !(c_termios.c_iflag & IXON)   &&
           !(c_termios.c_iflag & ICRNL)  &&
           !(c_termios.c_oflag & OPOST);
}