#include <sys/utsname.h>
#include <unistd.h>

int uname(struct utsname *buf)
{
    return (int)syscall6(SYSCALL_UNAME, (uint64_t)buf, 0, 0, 0, 0, 0);
}