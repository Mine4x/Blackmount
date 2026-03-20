#include "console.h"
#include <proc/proc.h>
#include <debug.h>

static InputManager *input = NULL;

vector waitingProcesses;

static uint32_t fg_color = 0xFFFFFF;
static uint32_t bg_color = 0x000000;

static bool c_escape_mode = false;
static char c_escape_buf[26];
static int  c_escape_pos  = 0;

void console_clear_text()
{
    tr_clear();
    draw_cursor();
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
                    case  0: fg = 0xFFFFFF; bg = 0x000000; break;
                    case 30: fg = 0x000000; break;
                    case 31: fg = 0xFF0000; break;
                    case 32: fg = 0x00FF00; break;
                    case 33: fg = 0xFFFF00; break;
                    case 34: fg = 0x0000FF; break;
                    case 35: fg = 0xFF00FF; break;
                    case 36: fg = 0x00FFFF; break;
                    case 37: fg = 0xFFFFFF; break;
                    case 40: bg = 0x000000; break;
                    case 41: bg = 0xFF0000; break;
                    case 42: bg = 0x00FF00; break;
                    case 43: bg = 0xFFFF00; break;
                    case 44: bg = 0x0000FF; break;
                    case 45: bg = 0xFF00FF; break;
                    case 46: bg = 0x00FFFF; break;
                    case 47: bg = 0xFFFFFF; break;
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