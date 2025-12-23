#include <input/keyboard/ps2.h>
#include <heap.h>
#include <memory.h>
#include <debug.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} InputBuffer;

static void input_init(InputBuffer *buf) {
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

static void input_grow(InputBuffer *buf, size_t new_capacity) {
    char *new_data = (char *)kmalloc(new_capacity);
    if (!new_data) {
        log_crit("shell", "kmalloc failed while growing input buffer");
        return;
    }

    if (buf->data) {
        memcpy(new_data, buf->data, (uint16_t)buf->size);
        kfree(buf->data);
    }

    buf->data = new_data;
    buf->capacity = new_capacity;
}

static void input_push(InputBuffer *buf, char c) {
    if (buf->size + 1 >= buf->capacity) {
        size_t new_capacity = (buf->capacity == 0) ? 32 : buf->capacity * 2;
        input_grow(buf, new_capacity);
    }

    buf->data[buf->size++] = c;
    buf->data[buf->size] = '\0';
}

static void input_pop(InputBuffer *buf) {
    if (buf->size == 0) {
        return;
    }

    buf->size--;
    buf->data[buf->size] = '\0';
}

static void input_clear(InputBuffer *buf) {
    buf->size = 0;
    if (buf->data)
        buf->data[0] = '\0';
}

void mountshell_start() {
    log_info("shell", "starting mount shell");

    keyboard_init();
    log_ok("shell", "keyboard initialized");

    InputBuffer input;
    input_init(&input);

    printf("> ");

    while (1) {
        if (!keyboard_has_input())
            continue;

        char c = keyboard_getchar();
        if (c == 0)
            continue;
        
        if (c == '\n') {
            printf("\n");

            if (input.size > 0) {
                /* TODO: parse + execute */
            }

            input_clear(&input);
            printf("> ");
            continue;
        }

        if (c == '\b' || c == 127) {
            input_pop(&input);
            printf("\b \b");
            continue;
        }

        input_push(&input, c);
        printf("%c", c);
    }
}
