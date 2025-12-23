#include <input/keyboard/ps2.h>
#include <heap.h>
#include <memory.h>
#include <debug.h>
#include <stddef.h>
#include <stdio.h>
#include <fs/fs.h>
#include <arch/i686/vga_text.h>
#include <string.h>

#define LS_BUFFER_SIZE 1024
#define MAX_ARGS 16

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} InputBuffer;

char PWD[256] = "/";

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
    if (buf->size == 0) return;
    buf->size--;
    buf->data[buf->size] = '\0';
}

static void input_clear(InputBuffer *buf) {
    buf->size = 0;
    if (buf->data) buf->data[0] = '\0';
}

void set_pwd(const char* new_dir) {
    if (fs_is_dir(new_dir)) {
        str_cpy(PWD, new_dir);
    } else {
        log_warn("cd", "given path isnt a directory");
    }
}

typedef struct {
    char* argv[MAX_ARGS];
    int argc;
} Args;

static void parse_args(char* input, Args* args) {
    args->argc = 0;
    while (*input) {
        while (*input == ' ') input++; // skip spaces
        if (*input == 0) break;
        if (args->argc >= MAX_ARGS) break;

        args->argv[args->argc++] = input;

        // null-terminate argument
        while (*input && *input != ' ') input++;
        if (*input == ' ') {
            *input = '\0';
            input++;
        }
    }
}

void buildin_ls(const char* path) {
    char buffer[LS_BUFFER_SIZE];
    int result = get_dir_cont(path, buffer, LS_BUFFER_SIZE);

    if (result < 0) {
        log_err("shell", "ls failed on path: %s", path);
        return;
    }

    int i = 0;
    while (i < result && buffer[i]) {
        char* entry = buffer + i;
        int len = 0;
        while (buffer[i] && buffer[i] != '\n') {
            i++;
            len++;
        }

        for (int j = 0; j < len; j++)
            printf("%c", entry[j]);
        printf("\n");

        if (buffer[i] == '\n') i++;
    }
}

void mountshell_start() {
    keyboard_init();

    InputBuffer input;
    input_init(&input);

    printf("\x1b[36mMountshell v0.0.1\n");
    printf("\x1b[36mType 'help' or '?' for a list of commands\x1b[0m\n");

    printf("\x1b[0m%s> ", PWD);

    while (1) {
        if (!keyboard_has_input()) continue;

        char c = keyboard_getchar();
        if (c == 0) continue;

        if (c == '\n') {
            printf("\n");

            if (input.size > 0) {
                Args args;
                parse_args(input.data, &args);

                if (args.argc > 0) {
                    if (str_cmp(args.argv[0], "cd") == 0) {
                        if (args.argc > 1) {
                            set_pwd(args.argv[1]);
                        } else {
                            printf("cd: missing argument\n");
                        }
                    } else if (str_cmp(args.argv[0], "ls") == 0) {
                        if (args.argc > 1)
                            buildin_ls(args.argv[1]);
                        else
                            buildin_ls(PWD);
                    } else if (str_cmp(args.argv[0], "help") == 0 || str_cmp(args.argv[0], "?") == 0) {
                        printf("Available commands: cd, ls, help, ?\n");
                    } else {
                        printf("Unknown command: %s\n", args.argv[0]);
                    }
                }
            }

            input_clear(&input);
            printf("%s> ", PWD);
            continue;
        }

        if (c == '\b' || c == 127) {
            if (input.size > 0) {
                input_pop(&input);
                VGA_backspace();
            }
            continue;
        }

        input_push(&input, c);
        printf("%c", c);
    }
}
