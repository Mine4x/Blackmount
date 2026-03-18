#include <syscalls.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    for (int i = 1; i < 11; i++)
    {
        printf("Hello, World number %d!\n", i);
    }

    return 0;
}