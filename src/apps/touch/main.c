#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <pathutil.h>

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

    int r = create(resolved, false);
    if (r < 0) {
        printf("Unable to create file: %s\n", resolved);
        return -1;
    }

    return 0;
}