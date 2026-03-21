#include "log.h"
#include <stdio.h>

void log_ok(const char* string)
{
    printf("[  \x1b[32mOK\x1b[0m  ] %s\n", string);
}

void log_fail(const char* string) {
    printf("[\033[1;31mFAILED\x1b[0m] %s\n", string);
}
