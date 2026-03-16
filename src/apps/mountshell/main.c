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

static void binary_check_and_execute(char* path, char* input)
{
    char ipath[124];

    strcpy(ipath, path);
    strcat(ipath, input);

    int fd = open(path);
    if (fd < 0)
    {
        return;
    }

    printf("Got binary\n");
    
    // TODO: Open binary

    close(fd);
}

int main()
{
    bool should_exit = false;
    bool should_block = false;

    printf("Mountshell v0.0.1\nBuild for BlackmountOS\n");

    while (!should_exit)
    {
        if (should_block)
        {
            
        }

        print_prefix();

        char path[124];
        scanf("%s", path);

        should_exit = check_inbuilt(&path);

        binary_check_and_execute("/bin/", &path);
    }
    

    return 0;
}