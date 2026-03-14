#include <syscalls.h>

extern int main();

void _start()
{
    int r = main();
    
    exit();
    
    while (1);
}