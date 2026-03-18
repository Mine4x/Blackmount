#include <syscall.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void print_prefix(void)
{
    printf("$ ");
}

static bool check_inbuilt(char* path)
{
    if (strcmp("exit", path) == 0)
    {
        return true;
    }

    return false;
}

static void binary_check_and_execute(const char* prefix, const char* input)
{
    char ipath[124];

    strcpy(ipath, prefix);
    strcat(ipath, input);

    int fd = open(ipath);
    if (fd < 0) {
        printf("No such binary: %s\n", ipath);
        return;
    }

    printf("Got binary with fd: %d\n", fd);
    
    close(fd);

    syscall6(301, (uint64_t)ipath, (uint64_t)10, 0, 0, 0, 0);
}

int main()
{
    bool should_exit = false;

    printf("Mountshell v0.0.1\nBuild for BlackmountOS\n");

    while (!should_exit)
    {
        print_prefix();

        char path[124];
        scanf("%s", path);

        should_exit = check_inbuilt(path);

        binary_check_and_execute("/bin/", path);
    }
    

    return 0;
}