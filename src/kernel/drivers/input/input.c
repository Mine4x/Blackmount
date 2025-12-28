#include <drivers/input/input.h>
#include <stdio.h>
#include <stdbool.h>
#include <debug.h>
#include <string.h>
#include <arch/i686/vga_text.h>

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} InputBuffer;

static InputBuffer g_input_buffer = {0};
static bool print_c = false;

static void set_print_c(bool newValue) {
    print_c = newValue;
}

static void input_init(void) {
    g_input_buffer.data = NULL;
    g_input_buffer.size = 0;
    g_input_buffer.capacity = 0;
}

static void input_grow(size_t new_capacity) {
    char *new_data = (char *)kmalloc(new_capacity);
    if (!new_data) {
        log_crit("input", "kmalloc failed while growing input buffer");
        return;
    }
    if (g_input_buffer.data) {
        memcpy(new_data, g_input_buffer.data, (uint16_t)g_input_buffer.size);
        kfree(g_input_buffer.data);
    }
    g_input_buffer.data = new_data;
    g_input_buffer.capacity = new_capacity;
}

static void input_push(char c) {
    if (g_input_buffer.size + 1 >= g_input_buffer.capacity) {
        size_t new_capacity = (g_input_buffer.capacity == 0) ? 32 : g_input_buffer.capacity * 2;
        input_grow(new_capacity);
    }
    g_input_buffer.data[g_input_buffer.size++] = c;
    g_input_buffer.data[g_input_buffer.size] = '\0';
}

static void input_pop(void) {
    if (g_input_buffer.size == 0) return;
    g_input_buffer.size--;
    g_input_buffer.data[g_input_buffer.size] = '\0';
}

static void input_clear(void) {
    g_input_buffer.size = 0;
    if (g_input_buffer.data) g_input_buffer.data[0] = '\0';
}

char* input_wait_and_get(void) {
    set_print_c(true);

    while (1) {
        if (g_input_buffer.size > 0 && 
            g_input_buffer.data[g_input_buffer.size - 1] == '\n') {
            break;
        }
        __asm__ volatile("hlt");
    }
    
    size_t len = g_input_buffer.size - 1;
    char *result = (char *)kmalloc(len + 1);
    if (!result) {
        log_crit("input", "kmalloc failed in input_wait_and_get");
        input_clear();
        set_print_c(false);
        return NULL;
    }
    
    memcpy(result, g_input_buffer.data, len);
    result[len] = '\0';
    
    input_clear();
    
    set_print_c(false);
    return result;
}

void handle_input(char c) {
    if (c == 0) {
        return;
    }
    if (c == '\b' || c == 127) {
        if (g_input_buffer.size > 0 && print_c) { 
            input_pop(); 
            VGA_backspace(); 
        }
        return;
    }
    if (c == '\n') {
        input_push(c);
        if (print_c) {
            printf("%c", c);
        }
        return;
    }
    input_push(c);
    if (print_c) {
        printf("%c", c);
    }
}