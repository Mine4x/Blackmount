#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include "log.h"
#include "manager.h"
#include "parser.h"

#define SYS_GETDENTS64 217
#define BUF_SIZE       4096

#define EXT2_FT_REG_FILE 1

typedef parse_respond (*parse_fn)(const char*);

static void scan_and_parse(const char* dir, parse_fn fn, const char* label);

typedef struct {
    int  uid;
    char name[64];
    char home[256];
    char shell[256];
} user_info_t;

static int authenticate(user_info_t *info);

static void strip_newline(char *s)
{
    while (*s) {
        if (*s == '\n' || *s == '\r') {
            *s = '\0';
            return;
        }
        s++;
    }
}

static int launch_with_env(const char *path, const user_info_t *info)
{
    char env_home [272];  /* "HOME="  + 256 */
    char env_shell[272];  /* "SHELL=" + 256 */
    char env_pwd  [272];  /* "PWD="   + 256 */
    char env_user [80];   /* "USER="  + 64  */
    char env_uid  [32];   /* "UID="   + 10  */

    snprintf(env_home,  sizeof(env_home),  "HOME=%s",  info->home);
    snprintf(env_shell, sizeof(env_shell), "SHELL=%s", info->shell);
    snprintf(env_pwd,   sizeof(env_pwd),   "PWD=%s",   info->home);
    snprintf(env_user,  sizeof(env_user),  "USER=%s",  info->name);
    snprintf(env_uid,   sizeof(env_uid),   "UID=%d",   info->uid);

    const char *envp[] = {
        env_home,
        env_shell,
        env_pwd,
        env_user,
        env_uid,
        NULL
    };

    const char *argv[] = { path, NULL };

    int pid = execve(path, argv, envp);
    return pid;
}

int main(void)
{
    if (manager_init() < 0) {
        log_fail("FATAL: Unable to init group and service manager!");
        return -1;
    }

    scan_and_parse("/etc/misys/groups",   parse_and_register_group,   "group");
    scan_and_parse("/etc/misys/services", parse_and_register_service, "service");

    manager_exec_all();

    log_ok("Started System completely");
    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\033[0m\n");

    while (1) {
        user_info_t *info = malloc(sizeof(user_info_t));
        if (!info) {
            log_fail("Failed to allocate user_info");
            return -1;
        }

        int uid = authenticate(info);
        if (uid >= 0) {
            printf("Home: %s\n",  info->home);
            printf("Shell: %s\n", info->shell);

            int pid = launch_with_env(info->shell, info);
            free(info);

            if (pid < 0) {
                printf("Unable to start shell — falling back to mountshell\n");

                user_info_t fallback = {
                    .uid   = 0,
                    .name  = "root",
                    .home  = "/",
                    .shell = "/bin/mountshell",
                };
                pid = launch_with_env("/bin/mountshell", &fallback);
                if (pid < 0) {
                    printf("Unable to start fallback shell!\nExiting misys!\n");
                    return -1;
                }
            }

            waitpid(pid);
        } else {
            printf("Login failed\n");
            free(info);
        }
    }

    return 0;
}

static int authenticate(user_info_t *info)
{
    char name[64];
    char pass[64];

    printf("Username: ");
    scanf("%s", name);
    printf("Password: ");
    scanf("%s", pass);

    strip_newline(name);
    strip_newline(pass);

    int uid = user_authenticate(name, pass);
    if (uid < 0)
        return -1;

    info->uid = uid;
    strncpy(info->name, name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';

    user_get_home (uid, info->home,  sizeof(info->home));
    user_get_shell(uid, info->shell, sizeof(info->shell));

    return uid;
}

static void scan_and_parse(const char* dir, parse_fn fn, const char* label)
{
    int fd = open(dir);
    if (fd < 0) {
        log_fail("Unable to open directory: ");
        printf("%s\n", dir);
        return;
    }

    char* buf = malloc(BUF_SIZE);
    if (!buf) {
        log_fail("Out of memory while scanning directory");
        close(fd);
        return;
    }

    int found = 0;
    int failed = 0;

    for (;;) {
        long nread = syscall6(SYS_GETDENTS64, (long)fd, (long)buf, BUF_SIZE, 0, 0, 0);
        if (nread <= 0)
            break;

        long offset = 0;
        while (offset < nread) {
            struct linux_dirent64* d = (struct linux_dirent64*)(buf + offset);
            offset += d->d_reclen;

            if (d->d_type != EXT2_FT_REG_FILE)
                continue;

            size_t namelen = strlen(d->d_name);
            if (namelen < 4 || strcmp(d->d_name + namelen - 4, ".ini") != 0)
                continue;

            size_t dirlen  = strlen(dir);
            size_t pathlen = dirlen + 1 + namelen + 1;
            char* path = malloc(pathlen);
            if (!path) {
                log_fail("Out of memory building path");
                continue;
            }

            memcpy(path, dir, dirlen);
            path[dirlen] = '/';
            memcpy(path + dirlen + 1, d->d_name, namelen + 1);

            parse_respond r = fn(path);
            if (r < 0) {
                printf("[misys] WARN: Failed to parse %s (%s): %d\n", label, path, r);
                failed++;
            } else {
                printf("[misys] Registered %s: %s\n", label, path);
                found++;
            }

            free(path);
        }
    }

    printf("[misys] %s: %d registered, %d failed\n", label, found, failed);

    free(buf);
    close(fd);
}