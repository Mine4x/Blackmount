#include <syscall.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_ARGS    32
#define INPUT_SIZE  256
#define PATH_SIZE   128

static void print_prefix(void)
{
    printf("$ ");
}

static bool check_inbuilt(const char *cmd)
{
    return strcmp(cmd, "exit") == 0;
}

static int parse_args(char *input, char **argv, int max_args)
{
    int   argc = 0;
    char *p    = input;

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

static void binary_check_and_execute(const char *prefix, char *input)
{
    char  buf[INPUT_SIZE];
    char *argv[MAX_ARGS];
    char  ipath[PATH_SIZE];

    strncpy(buf, input, INPUT_SIZE - 1);
    buf[INPUT_SIZE - 1] = '\0';

    int argc = parse_args(buf, argv, MAX_ARGS);
    if (argc == 0)
        return;

    strncpy(ipath, prefix, PATH_SIZE - 1);
    ipath[PATH_SIZE - 1] = '\0';
    strncat(ipath, argv[0], PATH_SIZE - strlen(ipath) - 1);

    int fd = open(ipath);
    if (fd < 0) {
        printf("No such binary: %s\n", ipath);
        return;
    }
    close(fd);

    argv[0] = ipath;

    int pid = execv(ipath, (const char **)argv);
    if (pid < 0) {
        printf("Failed to execute: %s\n", ipath);
        return;
    }

    waitpid(pid);
}

int main()
{
    printf("Mountshell v0.0.1\nBuilt for BlackmountOS\n");

    while (true) {
        print_prefix();

        char input[INPUT_SIZE];

        if (!fgets(input, sizeof(input), STDIN))
            continue;

        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n')
            input[len - 1] = '\0';

        if (input[0] == '\0')
            continue;

        char  first[INPUT_SIZE];
        char *space;

        strncpy(first, input, INPUT_SIZE - 1);
        first[INPUT_SIZE - 1] = '\0';

        space = strchr(first, ' ');
        if (space)
            *space = '\0';

        if (check_inbuilt(first)) {
            printf("Goodbye!\n");
            return 0;
        }

        binary_check_and_execute("/bin/", input);
    }

    return 0;
}