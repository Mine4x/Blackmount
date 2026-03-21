#include <stdio.h>
#include <syscalls.h>

#include <pathutil.h>

#define BUF_SIZE 4096

static int cat_file(const char *path)
{
    int fd = open(path);
    if (fd < 0) {
        printf("cat: %s: No such file or directory\n", path);
        return -1;
    }

    char *buf = malloc(BUF_SIZE);
    if (!buf) {
        close(fd);
        printf("cat: out of memory\n");
        return -1;
    }

    int n;
    while ((n = read(fd, buf, BUF_SIZE - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    free(buf);
    close(fd);
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    if (argc < 2) {
        printf("Usage: cat <file> [file...]\n");
        return 1;
    }

    const char *pwd  = getenv_local("PWD",  envp);
    const char *home = getenv_local("HOME", envp);

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        char resolved[PATH_SIZE];
        build_path(resolved, pwd ? pwd : "/", argv[i], home);
        if (cat_file(resolved) < 0)
            ret = 1;
    }

    return ret;
}