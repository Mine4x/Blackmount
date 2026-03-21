#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>

typedef struct
{
    char name[124];
} group;


typedef struct
{
    group* group;
    const char* name;
    const char* description;
    const char* exec;
    group** after;
    size_t after_count;
} service;


#endif