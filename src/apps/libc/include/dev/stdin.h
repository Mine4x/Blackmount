#ifndef STDIN_H
#define STDIN_H

#include <unistd.h>

#define STDIN_RMC 1
#define STDIN_CLEAR 2
#define STDIN_READ_C 3

int stdin_read_c(int fd, char *output);
int stdin_clear(int fd);
int stdin_rmc(int fd);

#endif