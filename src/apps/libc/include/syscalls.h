#ifndef SYSCALLS_H
#define SYSCALLS_H

#define SYSCALL_EXIT 60
#define SYSCALL_WRITE 1

/*
    Exits the current running task
*/
void exit(void);

#endif