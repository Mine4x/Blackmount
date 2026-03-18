#ifndef MANAGER_H
#define MANAGER_H

#define MAX_GROUPS 124
#define MAX_SERVICES 2000

#include <stdlib.h>
#include <string.h>
#include "types.h"

int manager_init(void);
int manager_register_group(const char* name);

#endif