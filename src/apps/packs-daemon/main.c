#include <stdio.h>
#include <unistd.h>

#include "manager.h"
#include "daemon.h"

int main()
{
    if (manager_init() < 0)
    {
        errorf("packs FATAL: unable to initialise package manager\n");
        return -1;
    }

    
    daemon_start();

    errorf("packs FATAL: daemon returned unexpectedly\n");
    return -1;
}