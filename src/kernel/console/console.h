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
#include <drivers/usb/xhci/usb_hid.h>

#define CONSOLE_BUFFER_SIZE 128
#define CONSOLE_MODULE "Console"

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

#endif