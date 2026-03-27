#ifndef TYPES_H
#define TYPES_H

typedef enum package_type
{
    EXECUTABLE = 0,
    SERVICE    = 1,
} package_type_t;

typedef struct package
{
    char*          name;
    char*          location;
    int            int_ver;
    char*          str_ver;
    package_type_t type;
} package_t;


#endif