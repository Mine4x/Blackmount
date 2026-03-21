#include <string.h>
#include <stdlib.h>
#include <stdint.h>

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && *s1 == *s2) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

void strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++));
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *ret = dst;
    while (n && *src) { *dst++ = *src++; n--; }
    while (n--)        *dst++ = '\0';
    return ret;
}

void strcat(char *dst, const char *src)
{
    dst += strlen(dst);
    while ((*dst++ = *src++));
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *ret = dst;
    dst += strlen(dst);
    while (n && *src) { *dst++ = *src++; n--; }
    *dst = '\0';
    return ret;
}

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char  *buf = malloc(len);
    if (buf)
        memcpy(buf, s, len);
    return buf;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strtok(char *str, const char *delim)
{
    static char *saved = NULL;
    if (str) saved = str;
    if (!saved || !*saved) return NULL;

    while (*saved && strchr(delim, *saved)) saved++;
    if (!*saved) return NULL;

    char *start = saved;
    while (*saved && !strchr(delim, *saved)) saved++;
    if (*saved) *saved++ = '\0';

    return start;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t       *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memset(void *dest, int c, size_t n)
{
    uint8_t *d = dest;
    while (n--) *d++ = (uint8_t)c;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t       *d = dest;
    const uint8_t *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = a, *pb = b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++; pb++;
    }
    return 0;
}