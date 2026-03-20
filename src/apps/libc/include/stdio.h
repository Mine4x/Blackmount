#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <syscalls.h>

#define STDIN 0
#define STDOUT 1

void putc(char c);
void puts(const char* str);
void print_int(int value);
void print_hex(unsigned int value);
void printf(const char* fmt, ...);

char *fgets(char *buf, int size, int fd);
void scanf(const char* fmt, ...);

int itoa(int value, char *buf);
int snprintf(char *out, int size, const char *fmt, ...);

#endif