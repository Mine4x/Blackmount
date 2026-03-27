#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define BUFSIZ 1024
#define EOF    (-1)

#define _FILE_READ  0x01
#define _FILE_WRITE 0x02
#define _FILE_EOF   0x04
#define _FILE_ERR   0x08

typedef struct {
    int  fd;
    int  flags;
    char buf[BUFSIZ];
    int  buf_pos;
    int  buf_len;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *f);
int    fread(void *ptr, int size, int count, FILE *f);
int    fwrite(const void *ptr, int size, int count, FILE *f);
int    fgetc(FILE *f);
int    fputc(int c, FILE *f);
int    fputs(const char *str, FILE *f);
char  *fgets(char *buf, int size, FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
void   clearerr(FILE *f);

int    vfprintf(FILE *f, const char *fmt, va_list args);
int    fprintf(FILE *f, const char *fmt, ...);
int    vprintf(const char *fmt, va_list args);
int    printf(const char *fmt, ...);
int    vsnprintf(char *str, int size, const char *fmt, va_list args);
int    snprintf(char *str, int size, const char *fmt, ...);
int    vsprintf(char *str, const char *fmt, va_list args);
int    sprintf(char *str, const char *fmt, ...);

void   scanf(const char *fmt, ...);

int    atoi(const char *s);
int    itoa(int value, char *buf);

void   putc(char c);
void   puts(const char *str);

#endif