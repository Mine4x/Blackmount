#include <syscalls.h>

int main(void)
{
    uint64_t r = syscall6(2, 10, 0 ,0 ,0, 0, 0);
    syscall6(2, r, 0, 0, 0, 0, 0);

    return 0;
}