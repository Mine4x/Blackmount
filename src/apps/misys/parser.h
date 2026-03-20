#ifndef PARSER_H
#define PARSER_H

#include <syscalls.h>
#include <string.h>
#include <stdlib.h>

#include "manager.h"

typedef enum
{
    OK = 0,
    MANAGER_ERROR = -1,
    NO_FILE = -2,
    MEMMORY = -3,
    INVALID = -4,
} parse_respond;

parse_respond parse_and_register_service(const char* path);
parse_respond parse_and_register_group(const char* path);

#endif