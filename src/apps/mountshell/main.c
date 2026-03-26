#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/utsname.h>

#include <pathutil.h>

#include "parser.h"

#define VERSION "1.0.0"

#define MAX_ARGS    32
#define INPUT_SIZE  256
#define MAX_ENV     32

static int parse_args(char *input, char **argv, int max_args);

static char *envp[MAX_ENV];
static int   envc = 0;

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
    const char *pwd  = get_env("PWD");
    const char *home = get_env("HOME");
    const char *user = get_env("USER");

    struct utsname ubuf;
    if (uname(&ubuf) != 0)
        return;

    const char *display_path;
    char tilde_path[PATH_SIZE];

    if (pwd && home && strncmp(pwd, home, strlen(home)) == 0) {
        const char *relative = pwd + strlen(home);
        if (*relative == '\0') {
            display_path = "~";
        } else {
            snprintf(tilde_path, sizeof(tilde_path), "~%s", relative);
            display_path = tilde_path;
        }
    } else {
        display_path = pwd ? pwd : "/";
    }

    printf("\x1b[31m%s\x1b[0m@%s:%s $ ",
           user             ? user             : "?",
           ubuf.nodename[0] ? ubuf.nodename    : ubuf.sysname,
           display_path);
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

static void handle_cd(char *input)
{
    char *argv[MAX_ARGS];
    char  buf[INPUT_SIZE];

    strncpy(buf, input, INPUT_SIZE - 1);
    buf[INPUT_SIZE - 1] = '\0';

    int argc = parse_args(buf, argv, MAX_ARGS);

    if (argc < 2) {
        printf("cd: missing argument\n");
        return;
    }

    const char *pwd  = get_env("PWD");
    const char *home = get_env("HOME");
    char newpath[PATH_SIZE];

    build_path(newpath, pwd ? pwd : "/", argv[1], home);

    int fd = open(newpath);
    if (fd < 0) {
        printf("cd: no such path\n");
        return;
    }
    close(fd);

    set_env("OLDPWD", pwd ? pwd : "/");
    set_env("PWD", newpath);
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

    if (argv[0][0] == '.' && (argv[0][1] == '/' || argv[0][1] == '\0' ||
        (argv[0][1] == '.' && (argv[0][2] == '/' || argv[0][2] == '\0')))) {
        const char *pwd  = get_env("PWD");
        const char *home = get_env("HOME");
        build_path(ipath, pwd ? pwd : "/", argv[0], home);
    } else if (argv[0][0] == '/') {
        strncpy(ipath, argv[0], PATH_SIZE - 1);
        ipath[PATH_SIZE - 1] = '\0';
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

int main(int argc, char **argv, char **inherited_envp)
{
    (void)argc; (void)argv;

    /* Seed internal env table from the environment execve handed us. */
    if (inherited_envp) {
        for (int i = 0; inherited_envp[i] && envc < MAX_ENV - 1; i++) {
            /* find the '=' to split key and value */
            const char *eq = strchr(inherited_envp[i], '=');
            if (!eq) continue;

            /* write a temporary null-terminated key */
            char key[PATH_SIZE];
            int  klen = eq - inherited_envp[i];
            if (klen >= PATH_SIZE) klen = PATH_SIZE - 1;
            strncpy(key, inherited_envp[i], klen);
            key[klen] = '\0';

            set_env(key, eq + 1);
        }
    }

    /* Fall back to "/" if PWD wasn't inherited. */
    if (!get_env("PWD"))
        set_env("PWD", "/");
    
    const char *home = get_env("HOME");
    char* cnfpath = malloc(sizeof(char)*512);

    sprintf(cnfpath, "%s/.mscnf", home);

    config_init(cnfpath);

    char *path = get_var("PATH", "/bin/");

    if (path[strlen(path)-1] != '/')
    {
        path[strlen(path)]='/';
    }

    set_env("PATH", path);

    printf("Mountshell v%s\nBuilt for BlackmountOS\n", VERSION);

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

        binary_check_and_execute(path, input);
    }

    return 0;
}