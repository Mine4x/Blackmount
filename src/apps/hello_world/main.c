#include <syscalls.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    printf("Give some input: ");

    int x;

    scanf("%d", &x);

    printf("got %d\n", x);

    return 0;
}