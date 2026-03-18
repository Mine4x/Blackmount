#include <stdio.h>
#include <syscalls.h>
#include "log.h"

int main()
{
    log_ok("Started System completly");

    printf("\n\nWelcome to \x1b[30;47mBlackmount\x1b[36;40m OS\033[0m\n");

    while (true)
    {
        int pid = binrun("/bin/mountshell");
        if (pid < 0)
        {
            printf("Unable to start intital program!\nExiting misys!\n");
            return -1;
        }
        waitpid(pid);
    }
    

    return 0;
}