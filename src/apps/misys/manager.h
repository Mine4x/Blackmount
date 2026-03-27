#ifndef MANAGER_H
#define MANAGER_H

#define MAX_GROUPS 124
#define MAX_SERVICES 2000

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include <stdio.h>
#include <unistd.h>
#include "log.h"

int manager_exec_all();

int manager_init(void);
int manager_register_group(const char* name);
int manager_register_service(group* grp, const char* name, const char* description, const char* exec, char* after_names);

group* find_group(const char* name);
bool group_exists(const char* name);

bool service_exists(const char* name);

#endif