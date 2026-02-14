#pragma once
#include <stdint.h>

static inline int str_to_int(const char* s) {
    int result = 0;
    int sign = 1;
    uint32_t i = 0;

    while (s[i] == ' ' || s[i] == '\t') i++;

    if (s[i] == '-') {
        sign = -1;
        i++;
    } else if (s[i] == '+') {
        i++;
    }

    while (s[i] >= '0' && s[i] <= '9') {
        result = result * 10 + (s[i] - '0');
        i++;
    }

    return result * sign;
}