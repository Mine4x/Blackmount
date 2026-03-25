#include "pci.h"
#include "pci_mod.h"
#include <heap.h>
#include <module/module.h>
#include <debug.h>

#define MOD "PCI-Module"

static int __mod_start(void)
{
    pci_init();
    return 0;
}

static void __mod_exit(void) { return; }

int pci_create_mod(void)
{
    module_t* mod = kmalloc(sizeof(module_t));
    if (!mod)
    {
        log_err(MOD, "Unable to allocate module");
        return -1;
    }

    strncpy(mod->name, "pci", sizeof(mod->name) - 1);
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