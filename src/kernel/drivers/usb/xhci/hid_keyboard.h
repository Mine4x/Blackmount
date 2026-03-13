#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "usb_hid.h"
#include "xhci.h"

#define HID_KEYBOARD_MOD "HID-KBD"

void hid_keyboard_init();

uint16_t hid_keyboard_read_key();

// Non-blocking: returns true if a key is available without
// waiting.  hid_keyboard_read_key() still needs to be called
// to consume it.
bool hid_keyboard_key_available();

// Returns true if the last read_key call encountered a hardware error.
bool hid_keyboard_has_error();

// Set keyboard LED state (Num Lock, Caps Lock, Scroll Lock).
void hid_keyboard_set_leds(hid_keyboard_leds_t leds);

// Per-keyboard device state
typedef struct {
    bool   active;
    xhci_controller_t* hc;
    xhci_device_t*     dev;

    uint8_t  intr_ep_addr;      // USB endpoint address for interrupt IN
    uint8_t  intr_ep_attrs;     // bmAttributes from the endpoint descriptor
    uint8_t  intr_ep_interval;  // bInterval from endpoint descriptor
    uint16_t intr_ep_max_pkt;   // wMaxPacketSize

    uint8_t config_value;      // bConfigurationValue to select
    uint8_t interface_num;     // bInterfaceNumber for SET_IDLE/SET_PROTOCOL

    // DMA buffer for the 8-byte interrupt IN report
    hid_keyboard_report_t* report_buf;

    // Previous report — used to detect newly pressed / released keys
    hid_keyboard_report_t prev_report;

    // Current modifier + caps lock state
    uint8_t modifiers;
    bool    caps_lock_active;

    // Error flag — set on transfer failure
    bool    error;
} hid_keyboard_device_t;

#endif // HID_KEYBOARD_H