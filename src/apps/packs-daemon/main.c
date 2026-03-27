#define VERSION "0.0.1"

#include <stdio.h>

#include "manager.h"

int main()
{
    if (manager_init() < 0)
    {
        printf("Unable to initiate manager!\n");
    }
}