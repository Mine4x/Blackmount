#include "ata.h"
#include "ata_mod.h"
#include <module/module.h>
#include <heap.h>
#include <string.h>
#include <debug.h>

#define MOD "ATA-Module"

static int __mod_start(void) { ata_init(); return 0; }

static void __mod_exit(void) { return; }

int ata_create_mod(void)
{
    module_t* mod = kmalloc(sizeof(module_t));
    if (!mod)
    {
        log_err(MOD, "Unable to allocate module");
        return -1;
    }

    strncpy(mod->name, "ata", sizeof(mod->name) - 1);
    mod->name[sizeof(mod->name) - 1] = '\0';
    mod->start = &__mod_start;
    mod->exit = &__mod_exit;

    if (module_register(mod) < 0)
    {
        log_err(MOD, "Unable to register module");
        kfree(mod);
        return -1;
    }

    return 0;
}