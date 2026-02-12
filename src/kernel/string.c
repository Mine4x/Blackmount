#include "string.h"

size_t str_len(const char* s) {
    size_t i = 0;
    while (s[i]) i++;
    return i;
}

int str_cmp(const char* s1, const char* s2) {
    size_t i = 0;
    while (s1[i] && s2[i] && s1[i] == s2[i]) i++;
    return (unsigned char)s1[i] - (unsigned char)s2[i];
}

void str_cpy(char* dst, const char* src) {
    size_t i = 0;
    while (src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0; // null terminator
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle)  // Empty needle
        return (char*)haystack;

    for (; *haystack; haystack++) {

        const char* h = haystack;
        const char* n = needle;

        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }

        if (!*n)  // Reached end of needle → match
            return (char*)haystack;
    }

    return NULL;
}


size_t strlen(const char* str) {
    return str_len(str);
}
void strcpy(char* dst, const char* src) {
    return str_cpy(dst, src);
}
int strcmp(const char* a, const char* b) {
    return str_cmp(a, b);
}