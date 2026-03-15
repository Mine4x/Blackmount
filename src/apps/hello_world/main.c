#include <syscalls.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    for (int i = 1; i < 15; i++)
    {
        printf("Number %d\n", i);
    }

    return 0;
}