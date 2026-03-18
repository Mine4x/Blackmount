#include <stdio.h>
#include <syscalls.h>
#include "log.h"
#include "manager.h"
#include "parser.h"

int main()
{
    if (manager_init() < 0)
    {
        log_fail("FATAL: Unable to init group and service manager!\n Exiting!\n");
        return -1;
    }
    
    manager_register_group("black");

    parse_respond r = parse_and_register_service("/etc/misys/exampleservice.ini");
    printf("Registerd service with %d\n", r);

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