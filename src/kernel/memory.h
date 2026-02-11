#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

void* memcpy(void* dst, const void* src, size_t num);
void* memset(void* ptr, int value, size_t num);
int memcmp(const void* ptr1, const void* ptr2, size_t num);

#endif // MEMORY_H