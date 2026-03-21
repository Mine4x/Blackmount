#ifndef STDOUT_H
#define STDOUT_H

#include <stddef.h>
#include <syscalls.h>

#define STDOUT_WRITE 1
#define STDOUT_PUTC 2
#define STDOUT_RMC 3

typedef struct
{
    size_t count;
    char* buf;
} stdout_write_request_t;

int stdout_rmc(int fd);
int stdout_putc(int fd, char* c);
int stdout_write(int fd, stdout_write_request_t* req);

#endif