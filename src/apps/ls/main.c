#include <syscalls.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <pathutil.h>

#define SYS_GETDENTS64  217
#define BUF_SIZE        4096

#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

static char type_char(unsigned char type)
{
    switch (type) {
    case EXT2_FT_DIR:      return 'd';
    case EXT2_FT_SYMLINK:  return 'l';
    case EXT2_FT_CHRDEV:   return 'c';
    case EXT2_FT_BLKDEV:   return 'b';
    case EXT2_FT_FIFO:     return 'p';
    case EXT2_FT_SOCK:     return 's';
    case EXT2_FT_REG_FILE: return '-';
    default:               return '?';
    }
}

int main(int argc, char **argv, char **envp)
{
    const char *pwd  = getenv_local("PWD",  envp);
    const char *home = getenv_local("HOME", envp);
    char resolved[PATH_SIZE];

    if (argc > 1) {
        build_path(resolved, pwd ? pwd : "/", argv[1], home);
    } else {
        strncpy(resolved, pwd ? pwd : "/", PATH_SIZE - 1);
        resolved[PATH_SIZE - 1] = '\0';
        normalize_path(resolved);
    }

    int fd = open(resolved);
    if (fd < 0) {
        printf("ls: failed to open %s\n", resolved);
        return 1;
    }

    char *buf = malloc(BUF_SIZE);
    if (!buf) {
        printf("ls: out of memory\n");
        close(fd);
        return 1;
    }

    for (;;) {
        long nread = syscall6(SYS_GETDENTS64,
                              (long)fd,
                              (long)buf,
                              (long)BUF_SIZE,
                              0, 0, 0);
        if (nread < 0) {
            printf("ls: getdents64 failed\n");
            break;
        }
        if (nread == 0)
            break;

        long offset = 0;
        while (offset < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + offset);
            printf("%c  %s\n", type_char(d->d_type), d->d_name);
            offset += d->d_reclen;
        }
    }

    free(buf);
    close(fd);
    return 0;
}