#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <syscalls.h>
#include "log.h"
#include "manager.h"
#include "parser.h"

#define SYS_GETDENTS64 217
#define BUF_SIZE       4096

struct linux_dirent64 {
    uint64_t       d_ino;
    int64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
} __attribute__((packed));

#define EXT2_FT_REG_FILE 1

typedef parse_respond (*parse_fn)(const char*);

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

int main(void)
{
    if (manager_init() < 0) {
        log_fail("FATAL: Unable to init group and service manager!\n Exiting!\n");
        return -1;
    }

    scan_and_parse("/etc/misys/groups",   parse_and_register_group,   "group");
    scan_and_parse("/etc/misys/services", parse_and_register_service, "service");

    log_ok("Started System completely");
    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\033[0m\n");

    while (true) {
        int pid = binrun("/bin/mountshell");
        if (pid < 0) {
            printf("Unable to start initial program!\nExiting misys!\n");
            return -1;
        }
        waitpid(pid);
    }

    return 0;
}