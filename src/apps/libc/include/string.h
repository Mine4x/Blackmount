#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

int strcmp(const char* a, const char* b);
void strcpy(char* dst, const char* src);
void strcat(char* dst, const char* src);
char* strdup(const char* s);
size_t strlen(const char* s);
char* strchr(const char* s, int c);
char* strtok(char* str, const char* delim);
int strncmp(const char* s1, const char* s2, size_t n);
void* memcpy(void* dest, const void* src, size_t n);

#endif
