#include <string.h>

int strcmp(const char* s1, const char* s2) {
    size_t i = 0;
    while (s1[i] && s2[i] && s1[i] == s2[i]) i++;
    return (unsigned char)s1[i] - (unsigned char)s2[i];
}

void strcpy(char* dst, const char* src) {
    size_t i = 0;
    while (src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0; // null terminator
}

void strcat(char* dst, const char* src) {
    size_t i = 0;
    size_t j = 0;

    // find end of destination string
    while (dst[i]) {
        i++;
    }

    // copy src to the end of dst
    while (src[j]) {
        dst[i + j] = src[j];
        j++;
    }

    // add null terminator
    dst[i + j] = 0;
}

char* strdup(const char* s)
{
    if (!s) return NULL;  // handle NULL input

    size_t len = strlen(s);
    char* copy = malloc(len + 1); // +1 for null terminator
    if (!copy) return NULL;       // allocation failed

    strcpy(copy, s);              // copy the string
    return copy;
}

size_t strlen(const char* s)
{
    size_t len = 0;
    while (s[len] != '\0')
        len++;
    return len;
}

char* strtok(char* str, const char* delim)
{
    static char* next = NULL;

    if (str) next = str;
    if (!next) return NULL;

    // Skip leading delimiters
    char* start = next;
    while (*start && strchr(delim, *start))
        start++;

    if (*start == '\0')  // no more tokens
    {
        next = NULL;
        return NULL;
    }

    // Find the end of this token
    char* end = start;
    while (*end && !strchr(delim, *end))
        end++;

    if (*end)
    {
        *end = '\0';  // terminate token
        next = end + 1;
    }
    else
    {
        next = NULL;  // last token
    }

    return start;
}

char* strchr(const char* s, int c)
{
    while (*s)
    {
        if (*s == (char)c)
            return (char*)s;
        s++;
    }
    return NULL;
}


int strncmp(const char* s1, const char* s2, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];

        if (c1 != c2)
            return c1 - c2;

        if (c1 == '\0')
            return 0;
    }

    return 0;
}

void* memcpy(void* dest, const void* src, size_t n)
{
    uint8_t*       d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    while (n && ((uintptr_t)d & 7)) {
        *d++ = *s++;
        n--;
    }

    uint64_t*       dw = (uint64_t*)d;
    const uint64_t* sw = (const uint64_t*)s;

    while (n >= 8) {
        *dw++ = *sw++;
        n -= 8;
    }

    d = (uint8_t*)dw;
    s = (const uint8_t*)sw;

    while (n--)
        *d++ = *s++;

    return dest;
}

void* memset(void* dest, int c, size_t n)
{
    uint8_t* d = (uint8_t*)dest;

    while (n && ((uintptr_t)d & 7)) {
        *d++ = (uint8_t)c;
        n--;
    }

    uint64_t  wide = (uint8_t)c;
    wide |= wide << 8;
    wide |= wide << 16;
    wide |= wide << 32;

    uint64_t* dw = (uint64_t*)d;
    while (n >= 8) {
        *dw++ = wide;
        n -= 8;
    }

    d = (uint8_t*)dw;
    while (n--)
        *d++ = (uint8_t)c;

    return dest;
}