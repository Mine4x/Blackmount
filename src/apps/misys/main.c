#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include "log.h"
#include "manager.h"
#include "parser.h"

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