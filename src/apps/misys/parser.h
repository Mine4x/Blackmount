#ifndef PARSER_H
#define PARSER_H

#include <syscalls.h>
#include <string.h>
#include <stdlib.h>

#include "manager.h"

typedef enum
{
    OK = 0,
    NO_FILE = -1,
    MALLOC = -2,
    INVALID = -3,
} parse_respond;

parse_respond parse_and_register_service(const char* path);

#endif