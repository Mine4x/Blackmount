#include <syscalls.h>
#include <stddef.h>

int main(void)
{
    uint64_t r = syscall6(2, 10, 0 ,0 ,0, 0, 0);
    syscall6(2, r, 0, 0, 0, 0, 0);

    const char* msg = "Hello, world!\n";
    size_t written = (size_t)write(1, msg, 14);
    if (written == (size_t)14) {
        msg = "Write completed with 14 bytes\n";
        write(1, msg, 30);
    }

    return 0;
}