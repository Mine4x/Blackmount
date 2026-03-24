#include "module.h"
#include <heap.h>
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
    module_count = 0;

    return 0;
}

int module_register(module_t* mod)
{
    if (module_count >= MAX_MODS)
    {
        log_err("Unable to register module %s: max kernel_modules reached", mod->name);
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