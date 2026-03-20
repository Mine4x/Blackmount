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

    int pid = execv(ipath, NULL);
    if (pid < 0)
        return;
    
    waitpid(pid);
}

int main()
{
    printf("Mountshell v0.0.1\nBuild for BlackmountOS\n");

    while (true)
    {
        print_prefix();

        char path[124];
        scanf("%s", path);

        if (check_inbuilt(path))
        {
            printf("Goodbye!\n");
            return 0;
        }

        binary_check_and_execute("/bin/", path);
    }
    

    return 0;
}