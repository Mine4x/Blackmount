#include "module.h"
#include <heap.h>
#include <string.h>
#include <debug.h>

#define MAX_MODS 120
#define MODULE "kernel_modules"

module_t **kernel_modules;
int module_count;

int module_init()
{
    kernel_modules = kmalloc(sizeof(module_t*) * MAX_MODS);
    if (!kernel_modules)
    {
        log_crit(MODULE, "Unable to allocate kernel modules buffer");
        return -1;
    }

    for (int i = 0; i < MAX_MODS; i++)
        kernel_modules[i] = NULL;

    module_count = 0;

    return 0;
}

int module_register(module_t* mod)
{
    if (module_count >= MAX_MODS)
    {
        log_err(MODULE, "Unable to register module %s: max kernel_modules reached", mod->name);
        return -1;
    }

    kernel_modules[module_count] = mod;
    module_count++;

    return 0;
}

void module_start()
{
    for (int i = 0; i < module_count; i++)
    {
        module_t* mod = kernel_modules[i];
        if (!mod)
        {
            log_err(MODULE, "Unable to start module %d: not a module", i);
            continue;
        }

        if (!mod->enabled)
        {
            log_warn(MODULE, "Skipping module %s: not enabled", mod->name);
            continue;
        }

        int r = mod->start();
        if (r < 0)
        {
            log_err(MODULE, "Unable to start module %s: start function returned with %d", mod->name, r);
            continue;
        }

        log_ok(MODULE, "Started module %s", mod->name);
    }
}

void module_exit()
{
    for (int i = 0; i < module_count; i++)
    {
        module_t* mod = kernel_modules[i];
        if (!mod)
        {
            log_err(MODULE, "Unable to exit module %d: not a module", i);
            continue;
        }

        mod->exit();

        log_ok(MODULE, "Exited module %s", mod->name);
    }
}


#include <string.h>
#include <stdbool.h>

void module_disable_others(const char* enabled_list)
{
    if (!enabled_list) return;

    char list_copy[512];
    strncpy(list_copy, enabled_list, sizeof(list_copy) - 1);
    list_copy[sizeof(list_copy) - 1] = '\0';

    char* allowed[MAX_MODS];
    int allowed_count = 0;
    char* token = strtok(list_copy, ",");
    while (token && allowed_count < MAX_MODS)
    {
        allowed[allowed_count++] = token;
        token = strtok(NULL, ",");
    }

    for (int i = 0; i < module_count; i++)
    {
        module_t* mod = kernel_modules[i];
        if (!mod) continue;

        bool found = false;
        for (int j = 0; j < allowed_count; j++)
        {
            if (strcmp(mod->name, allowed[j]) == 0)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            mod->enabled = false;
            log_warn(MODULE, "Disabled module %s: not in allowed list", mod->name);
        } else
        {
            mod->enabled = true;
        }
    }
}