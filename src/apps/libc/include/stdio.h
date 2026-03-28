#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#define STDIN 0
#define STDOUT 1
#define STDERR 2

int format(char *out, int size, const char *fmt, va_list args);

void putc(char c);
void puts(const char* str);
void print_int(int value);
void print_hex(unsigned int value);
void printf(const char* fmt, ...);
void errorf(const char* fmt, ...);

int fdprintf(int fd, const char* fmt, ...);
int vfdprintf(int fd, const char* fmt, va_list args);

char *fgets(char *buf, int size, int fd);
void scanf(const char* fmt, ...);

int itoa(int value, char *buf);
int snprintf(char *out, int size, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int atoi(const char *s);

#endif