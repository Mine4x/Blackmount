#include <module/module.h>
#include "hid_keyboard.h"
#include "xhci.h"
#include <arch/x86_64/io.h>
#include <debug.h>
#include <heap.h>

#define MOD "XHCI-Module"

static int __mod_start(void)
{
    log_info(MOD, "Starting XHCI module");

    hid_keyboard_init();
    log_debug(MOD, "Initialized HID keyboard");

    x86_64_EnableInterrupts();
    int response = xhci_init_device();
    if (response == 0) {
        log_ok(MOD, "Initialized xHCI");
        response = xhci_start_device();
        if (response == 0) {
            log_ok(MOD, "Started xHCI");
        } else {
            log_err(MOD, "xHCI init exited with error: %d", response);
        }
    } else {
        log_err(MOD, "xHCI init exited with error: %d", response);
    }
    x86_64_DisableInterrupts();
}

static void __mod_exit() {return;}

int xhci_create_mod(void)
{
    module_t* mod = kmalloc(sizeof(module_t));
    if (!mod)
    {
        log_err(MOD, "Unable to allocate module");
        return -1;
    }

    mod->name = "xHCI";
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