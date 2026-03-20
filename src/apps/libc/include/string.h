#ifndef STRING_H
#define STRING_H
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

int    strcmp(const char *a, const char *b);
int    strncmp(const char *s1, const char *s2, size_t n);
void   strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
void   strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);
char  *strdup(const char *s);
size_t strlen(const char *s);
char  *strchr(const char *s, int c);
char  *strtok(char *str, const char *delim);
void  *memcpy(void *dest, const void *src, size_t n);
void  *memset(void *dest, int c, size_t n);
void  *memmove(void *dest, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

#endif