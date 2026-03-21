#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <syscalls.h>
#include "log.h"
#include "manager.h"
#include "parser.h"

typedef struct {
    int uid;
    char home[256];
    char shell[256];
} user_info_t;

static int authenticate(user_info_t *info);

int main(void)
{
    if (manager_init() < 0) {
        log_fail("FATAL: Unable to init group and service manager!");
        return -1;
    }

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
            printf("Home: %s\n", info->home);
            printf("Shell: %s\n", info->shell);

            int pid = binrun(info->shell);
            free(info);

            if (pid < 0) {
                printf("Unable to start initial program!\nExiting misys!\n");
                int pid = binrun("/bin/mountshell");
                waitpid(pid);
                return -1;
            }
            waitpid(pid);
        } else {
            printf("Login failed\n");
            free(info);
        }
    }

    return 0;
}

void strip_newline(char *s)
{
    while (*s) {
        if (*s == '\n' || *s == '\r') {
            *s = 0;
            return;
        }
        s++;
    }
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
    user_get_home(uid, info->home, sizeof(info->home));
    user_get_shell(uid, info->shell, sizeof(info->shell));

    return uid;
}