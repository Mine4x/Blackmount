#ifndef STRING_H
#define STRING_H

#include <stddef.h>

size_t strlen(const char* str);
void strcpy(char* dst, const char* src);
int strcmp(const char* a, const char* b);
char* strstr(const char* haystack, const char* needle);
char *strchr(const char *s, int c);
char *strncpy(char *dest, const char *src, size_t n);
char *strdup(const char *s);
int strncmp(const char *s1, const char *s2, size_t n);
char* strtok(char* str, const char* delim);

#endif
