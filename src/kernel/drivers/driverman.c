#include <drivers/driverman.h>
#include <debug.h>
#include <drivers/input/keyboard/ps2.h>
#include <drivers/input/input.h>
#include <heap.h>

// TODO: Should scan for devices an load drivers but wont be implemented for now

static void input_keyboard_binding(char c) {
    handle_input(c);
}

void drivers_init(void) {
    log_info("Drivers", "Starting Keyboard drivers");
    ps2_keyboard_init();
    ps2_keyboard_bind(&input_keyboard_binding);
    log_info("Drivers", "Started Keyboard drivers");
    
    log_ok("Drivers", "All drivers started");

    printf("Waiting\n");
    char *x = input_wait_and_get();
    printf("Waited\n");

    printf("%s\n", x);

    kfree(x);
}