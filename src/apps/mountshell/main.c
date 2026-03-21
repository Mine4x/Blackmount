#include <syscalls.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_ARGS    32
#define INPUT_SIZE  256
#define PATH_SIZE   128
#define MAX_ENV     32

static int parse_args(char *input, char **argv, int max_args);

static char *envp[MAX_ENV];
static int envc = 0;

static void set_env(const char *key, const char *value)
{
    static char buffer[MAX_ENV][PATH_SIZE];

    for (int i = 0; i < envc; i++) {
        if (strncmp(envp[i], key, strlen(key)) == 0 &&
            envp[i][strlen(key)] == '=') {

            snprintf(buffer[i], PATH_SIZE, "%s=%s", key, value);
            envp[i] = buffer[i];
            return;
        }
    }

    if (envc < MAX_ENV) {
        snprintf(buffer[envc], PATH_SIZE, "%s=%s", key, value);
        envp[envc] = buffer[envc];
        envc++;
        envp[envc] = NULL;
    }
}

static const char *get_env(const char *key)
{
    for (int i = 0; i < envc; i++) {
        if (strncmp(envp[i], key, strlen(key)) == 0 &&
            envp[i][strlen(key)] == '=') {
            return envp[i] + strlen(key) + 1;
        }
    }
    return NULL;
}

static void print_prefix(void)
{
    const char *pwd = get_env("PWD");
    if (pwd)
        printf("%s $ ", pwd);
    else
        printf("$ ");
}

static int parse_args(char *input, char **argv, int max_args)
{
    int argc = 0;
    char *p = input;

    while (*p && argc < max_args - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }

    argv[argc] = NULL;
    return argc;
}

static void normalize_path(char *path)
{
    char tmp[PATH_SIZE];
    strncpy(tmp, path, PATH_SIZE - 1);
    tmp[PATH_SIZE - 1] = '\0';

    char *segs[32];
    int lens[32];
    int top = 0;

    char *p = tmp;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char *start = p;
        while (*p && *p != '/') p++;

        int len = p - start;

        if (len == 1 && start[0] == '.') {
            continue;
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (top > 0) top--;
        } else {
            segs[top] = start;
            lens[top] = len;
            top++;
        }
    }

    char result[PATH_SIZE];
    int pos = 0;

    if (top == 0) {
        result[pos++] = '/';
    } else {
        for (int i = 0; i < top; i++) {
            result[pos++] = '/';
            for (int j = 0; j < lens[i]; j++)
                result[pos++] = segs[i][j];
        }
    }

    result[pos] = '\0';
    strncpy(path, result, PATH_SIZE - 1);
    path[PATH_SIZE - 1] = '\0';
}

static void build_path(char *out, const char *pwd, const char *input)
{
    if (input[0] == '/') {
        strncpy(out, input, PATH_SIZE - 1);
    } else {
        snprintf(out, PATH_SIZE, "%s/%s", pwd, input);
    }
    out[PATH_SIZE - 1] = '\0';
    normalize_path(out);
}

static void handle_cd(char *input)
{
    char *argv[MAX_ARGS];
    char buf[INPUT_SIZE];

    strncpy(buf, input, INPUT_SIZE - 1);
    buf[INPUT_SIZE - 1] = '\0';

    int argc = parse_args(buf, argv, MAX_ARGS);

    if (argc < 2) {
        printf("cd: missing argument\n");
        return;
    }

    const char *pwd = get_env("PWD");
    char newpath[PATH_SIZE];

    build_path(newpath, pwd ? pwd : "/", argv[1]);

    int fd = open(newpath);

    if (fd < 0)
    {
        printf("cd: no such path\n");
        return;
    }

    close(fd);

    set_env("OLDPWD", pwd ? pwd : "/");
    set_env("PWD", newpath);
}

static void binary_check_and_execute(const char *prefix, char *input)
{
    char buf[INPUT_SIZE];
    char *argv[MAX_ARGS];
    char ipath[PATH_SIZE];

    strncpy(buf, input, INPUT_SIZE - 1);
    buf[INPUT_SIZE - 1] = '\0';

    int argc = parse_args(buf, argv, MAX_ARGS);
    if (argc == 0)
        return;

    if (argv[0][0] == '.' && (argv[0][1] == '/' || argv[0][1] == '\0' ||
        (argv[0][1] == '.' && (argv[0][2] == '/' || argv[0][2] == '\0')))) {
        const char *pwd = get_env("PWD");
        build_path(ipath, pwd ? pwd : "/", argv[0]);
    } else {
        strncpy(ipath, prefix, PATH_SIZE - 1);
        ipath[PATH_SIZE - 1] = '\0';
        strncat(ipath, argv[0], PATH_SIZE - strlen(ipath) - 1);
    }

    int fd = open(ipath);
    if (fd < 0) {
        printf("No such binary: %s\n", ipath);
        return;
    }
    close(fd);

    argv[0] = ipath;

    int pid = execve(ipath, (const char **)argv, (const char **)envp);
    if (pid < 0) {
        printf("Failed to execute: %s\n", ipath);
        return;
    }

    waitpid(pid);
}

int main()
{
    printf("Mountshell v0.0.1\nBuilt for BlackmountOS\n");

    set_env("PWD", "/");

    while (true) {
        print_prefix();

        char input[INPUT_SIZE];

        if (!fgets(input, sizeof(input), 0))
            continue;

        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n')
            input[len - 1] = '\0';

        if (input[0] == '\0')
            continue;

        char first[INPUT_SIZE];
        strncpy(first, input, INPUT_SIZE - 1);
        first[INPUT_SIZE - 1] = '\0';

        char *space = strchr(first, ' ');
        if (space)
            *space = '\0';

        if (strcmp(first, "exit") == 0) {
            printf("Goodbye!\n");
            return 0;
        }

        if (strcmp(first, "cd") == 0) {
            handle_cd(input);
            continue;
        }

        binary_check_and_execute("/bin/", input);
    }

    return 0;
}