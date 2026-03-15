#include <syscalls.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    printf("Give some input: ");

    char buf[104];
    read(STDIN, buf, sizeof(buf));

    printf("%s\n", buf);

    return 0;
}