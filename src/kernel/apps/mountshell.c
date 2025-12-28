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
#define MAX_PATH 256
#define MAX_CACHE_ENTRIES 32

typedef struct {
    char cmd_name[64];
    char full_path[MAX_PATH];
    int valid;
} CacheEntry;

static CacheEntry cmd_cache[MAX_CACHE_ENTRIES];
static int cache_initialized = 0;

char PWD[256] = "/";

static void resolve_path(const char* pwd, const char* input, char* out);

static void cache_init() {
    if (cache_initialized) return;
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        cmd_cache[i].valid = 0;
        cmd_cache[i].cmd_name[0] = 0;
        cmd_cache[i].full_path[0] = 0;
    }
    cache_initialized = 1;
}

static int cache_lookup(const char* cmd, char* path_out) {
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (cmd_cache[i].valid && str_cmp(cmd_cache[i].cmd_name, cmd) == 0) {
            str_cpy(path_out, cmd_cache[i].full_path);
            return 1;
        }
    }
    return 0;
}

static void cache_add(const char* cmd, const char* path) {
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (!cmd_cache[i].valid) {
            str_cpy(cmd_cache[i].cmd_name, cmd);
            str_cpy(cmd_cache[i].full_path, path);
            cmd_cache[i].valid = 1;
            return;
        }
    }
    str_cpy(cmd_cache[0].cmd_name, cmd);
    str_cpy(cmd_cache[0].full_path, path);
    cmd_cache[0].valid = 1;
}

static int try_execute_from_bin(const char* cmd) {
    char path[MAX_PATH];

    if (cache_lookup(cmd, path)) {
        execute_file(path);
        return 1;
    }

    str_cpy(path, "/bin/");
    int len = str_len(path);
    for (int i = 0; cmd[i] && len < MAX_PATH - 1; i++)
        path[len++] = cmd[i];
    path[len] = 0;

    if (fs_exists(path)) {
        cache_add(cmd, path);
        execute_file(path);
        return 1;
    }

    return 0;
}

static int try_execute_path(const char* input) {
    char resolved[MAX_PATH];
    resolve_path(PWD, input, resolved);

    if (fs_exists(resolved)) {
        execute_file(resolved);
        return 1;
    }
    return 0;
}

typedef struct {
    char* argv[MAX_ARGS];
    int argc;
} Args;

static void parse_args(char* input, Args* args) {
    args->argc = 0;
    while (*input) {
        while (*input == ' ') input++;
        if (!*input || args->argc >= MAX_ARGS) break;
        args->argv[args->argc++] = input;
        while (*input && *input != ' ') input++;
        if (*input) *input++ = 0;
    }
}

static void resolve_path(const char* pwd, const char* input, char* out) {
    char temp[MAX_PATH];
    int out_len = 0;

    if (input[0] == '/') {
        temp[0] = '/';
        temp[1] = 0;
        out_len = 1;
    } else {
        str_cpy(temp, pwd);
        out_len = str_len(temp);
    }

    int i = 0;
    while (input[i]) {
        while (input[i] == '/') i++;
        if (!input[i]) break;
        int start = i;
        while (input[i] && input[i] != '/') i++;
        int len = i - start;

        char comp[64];
        for (int j = 0; j < len && j < 63; j++)
            comp[j] = input[start + j];
        comp[len] = 0;

        if (str_cmp(comp, ".") == 0) continue;
        if (str_cmp(comp, "..") == 0) {
            if (out_len > 1) {
                out_len--;
                while (out_len > 0 && temp[out_len - 1] != '/') out_len--;
                temp[out_len] = 0;
            }
            continue;
        }

        if (out_len > 1 && temp[out_len - 1] != '/')
            temp[out_len++] = '/';

        for (int j = 0; comp[j] && out_len < MAX_PATH - 1; j++)
            temp[out_len++] = comp[j];
        temp[out_len] = 0;
    }

    if (out_len == 0) {
        temp[0] = '/';
        temp[1] = 0;
    }

    str_cpy(out, temp);
}

void set_pwd(const char* new_dir) {
    char resolved[MAX_PATH];
    resolve_path(PWD, new_dir, resolved);
    if (fs_is_dir(resolved))
        str_cpy(PWD, resolved);
    else
        log_warn("cd", "given path isnt a directory");
}

void buildin_ls(const char* path) {
    char buffer[LS_BUFFER_SIZE];
    char resolved[MAX_PATH];
    resolve_path(PWD, path, resolved);

    int result = get_dir_cont(resolved, buffer, LS_BUFFER_SIZE);
    if (result < 0) {
        log_err("ls", "ls failed on path: %s", resolved);
        return;
    }

    for (int i = 0; i < result && buffer[i]; ) {
        while (buffer[i] && buffer[i] != '\n')
            printf("%c", buffer[i++]);
        printf("\n");
        if (buffer[i] == '\n') i++;
    }
}

void buildin_mkdir(const char* path) {
    char resolved[MAX_PATH];
    resolve_path(PWD, path, resolved);
    if (create_dir(resolved) < 0)
        log_err("mkdir", "mkdir couldn't create path: %s", resolved);
}

void mountshell_start() {
    cache_init();
    printf("\x1b[36mMountshell v0.0.2\n");
    printf("\x1b[36mType 'help' or '?' for a list of commands\x1b[0m\n");

    while (1) {
        printf("\x1b[0m%s> ", PWD);

        char* line = input_wait_and_get();
        if (!line || !line[0]) continue;

        Args args;
        parse_args(line, &args);
        if (args.argc == 0) continue;

        if (str_cmp(args.argv[0], "cd") == 0) {
            if (args.argc > 1) set_pwd(args.argv[1]);
            else printf("cd: missing argument\n");
        } else if (str_cmp(args.argv[0], "ls") == 0) {
            if (args.argc > 1) buildin_ls(args.argv[1]);
            else buildin_ls(PWD);
        } else if (str_cmp(args.argv[0], "help") == 0 || str_cmp(args.argv[0], "?") == 0) {
            printf("Available commands: cd, ls, help, ?, mkdir\n");
        } else if (str_cmp(args.argv[0], "mkdir") == 0) {
            if (args.argc > 1) buildin_mkdir(args.argv[1]);
            else printf("mkdir: missing argument\n");
        } else {
            if (args.argv[0][0] == '/' ||
               (args.argv[0][0] == '.' && args.argv[0][1] == '/')) {
                if (!try_execute_path(args.argv[0]))
                    printf("Unknown command: %s\n", args.argv[0]);
            } else {
                if (!try_execute_from_bin(args.argv[0]))
                    printf("Unknown command: %s\n", args.argv[0]);
            }
        }
    }
}
