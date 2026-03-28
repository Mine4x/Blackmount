#ifndef STDLIB_H
#define STDLIB_H
#include <stddef.h>

void *malloc(size_t size);
void  free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);
void *zalloc(size_t size);

#endif