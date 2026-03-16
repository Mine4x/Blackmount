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