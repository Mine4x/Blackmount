#include <drivers/driverman.h>
#include <debug.h>
#include <drivers/input/keyboard/ps2.h>
#include <heap.h>
#include <stdio.h>
#include <drivers/disk/ata.h>
#include <drivers/disk/floppy.h>
#include <config/config.h>
#include <util/str_to_int.h>
#include <drivers/input/input.h>
#include <fb/textrenderer.h>
#include <hal/vfs.h>
#include <panic/panic.h>

#define DRIVERS_MODULE "Drivers"

int stdin_fd;

static void input_keyboard_binding(char c)
{
    if (c == '\b' || c == 127)
    {
        if (Input_get_length() == 0)
            return;

        Input_RmChar();
        tr_backspace();
        return;
    }

    if (c == '\n')
    {
        printf("\n");

        Input_Clear();

        return;
    }

    if (Input_AddChar(c))
    {
        printf("%c", c);
    }
}

void drivers_init(void)
{
    log_info(DRIVERS_MODULE, "Creating important driver file");

    VFS_Create("/dev/stdin", false);

    stdin_fd = VFS_Open("/dev/stdin");
    if (stdin_fd == -1)
        panic(DRIVERS_MODULE, "Couldn't create /dev/stdin");

    log_info(DRIVERS_MODULE, "Starting Keyboard drivers");

    ps2_keyboard_init();
    ps2_keyboard_bind(&input_keyboard_binding);

    log_info(DRIVERS_MODULE, "Started Keyboard drivers");

    log_info(DRIVERS_MODULE, "Starting Input manager");

    if (!Input_Init(stdin_fd))
        panic(DRIVERS_MODULE, "Failed to initialize Input Manager");

    log_debug(DRIVERS_MODULE, "Initialized Input Buffer");

    log_ok(DRIVERS_MODULE, "All drivers started");
}
