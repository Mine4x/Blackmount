#include <drivers/driverman.h>
#include <debug.h>
#include <drivers/input/keyboard/ps2.h>
#include <heap.h>
#include <stdio.h>
#include <drivers/disk/ata.h>
#include <drivers/disk/floppy.h>
#include <config/config.h>
#include <util/str_to_int.h>
#include <fb/textrenderer.h>
#include <hal/vfs.h>
#include <panic/panic.h>
#include <arch/x86_64/io.h>
#include <drivers/usb/xhci/hid_keyboard.h>
#include <proc/proc.h>
#include <console/console.h>

#define DRIVERS_MODULE "Drivers"

static void input_keyboard_binding(char c)
{
    if (c == '\b' || c == 127)
    {
        if (console_get_length() == 0)
            return;

        console_backspace();
        return;
    }

    if (c == '\n')
    {
        console_clear();
        printf("\n");

        return;
    }

    console_user_put_c(c);
}

static void hid_keyboard_loop() {
    if (hid_keyboard_key_available()) {
        uint16_t key = hid_keyboard_read_key();
        if (key == HID_SPECIAL_KEY_NONE) {return; console_reset_special_char();}

        switch (key) {
            case HID_SPECIAL_KEY_UP:        printf("<UP>"); console_add_special_char(key);    return;
            case HID_SPECIAL_KEY_DOWN:      printf("<DOWN>"); console_add_special_char(key);  return;
            case HID_SPECIAL_KEY_LEFT:      printf("<LEFT>"); console_add_special_char(key);  return;
            case HID_SPECIAL_KEY_RIGHT:     printf("<RIGHT>"); console_add_special_char(key); return;
            case HID_SPECIAL_KEY_F1 ... HID_SPECIAL_KEY_F12:
                printf("<F%d>", key - HID_SPECIAL_KEY_F1 + 1); console_add_special_char(key); return;
            case HID_SPECIAL_KEY_HOME:      printf("<HOME>"); console_add_special_char(key);  return;
            case HID_SPECIAL_KEY_END:       printf("<END>"); console_add_special_char(key);   return;
            case HID_SPECIAL_KEY_PAGE_UP:   printf("<PGUP>"); console_add_special_char(key);  return;
            case HID_SPECIAL_KEY_PAGE_DOWN: printf("<PGDN>"); console_add_special_char(key);  return;
            case HID_SPECIAL_KEY_INSERT:    printf("<INS>"); console_add_special_char(key);   return;
            case HID_SPECIAL_KEY_DELETE:    printf("<DEL>"); console_add_special_char(key);   return;
            case HID_SPECIAL_KEY_CAPS_LOCK: printf("<CAPS>"); console_add_special_char(key);  return;
            default:                        input_keyboard_binding((char)key); console_set_current_c((char)key);
        }
    }
}

void drivers_loop(void)
{
    while (true) {
        hid_keyboard_loop();
    }
}

void drivers_init(void)
{
    //log_info(DRIVERS_MODULE, "Starting Keyboard drivers");

    //ps2_keyboard_init();
    //ps2_keyboard_bind(&input_keyboard_binding);

    //log_info(DRIVERS_MODULE, "Started Keyboard drivers");

    void (*ptr)() = &drivers_loop;
    proc_create_kernel(ptr, 10, 0);

    log_info(DRIVERS_MODULE, "Starting Input manager");

    if (!console_init())
        panic(DRIVERS_MODULE, "Failed to initialize Input Manager");

    log_debug(DRIVERS_MODULE, "Initialized Input Buffer");

    log_ok(DRIVERS_MODULE, "All drivers started");
}
