#include <stdio.h>
#include <unistd.h>

#include "manager.h"
#include "daemon.h"

int main()
{
    if (manager_init() < 0)
    {
        printf("\x1b[31;41mpacks FATAL: UNABLE TO INITIATE MANAGER\x1b[0m\n");
    }

    daemon_start();

    printf("\x1b[31;41mpacks FATAL: DAEMON RETURNED\x1b[0m\n");

    return -1;
}