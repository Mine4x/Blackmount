#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>

// Interface Descriptor — USB 2.0 Spec Section 9.6.5
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;     // = USB_DESC_TYPE_INTERFACE (4)
    uint8_t bInterfaceNumber;    // Zero-based index of this interface
    uint8_t bAlternateSetting;   // Alternate setting index (0 = default)
    uint8_t bNumEndpoints;       // Number of endpoints (excluding EP0)
    uint8_t bInterfaceClass;     // Class code (3 = HID)
    uint8_t bInterfaceSubClass;  // Sub-class (0 = none, 1 = boot interface)
    uint8_t bInterfaceProtocol;  // Protocol (0=none, 1=keyboard, 2=mouse)
    uint8_t iInterface;          // String index (0 = no string)
} __attribute__((packed)) usb_interface_descriptor_t;
_Static_assert(sizeof(usb_interface_descriptor_t) == 9,
               "usb_interface_descriptor_t must be 9 bytes");

// Endpoint Descriptor — USB 2.0 Spec Section 9.6.6
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;   // = USB_DESC_TYPE_ENDPOINT (5)
    uint8_t  bEndpointAddress;  // Bits[3:0]=EP number, bit[7]=direction (1=IN)
    uint8_t  bmAttributes;      // Bits[1:0]: 0=ctrl,1=isoch,2=bulk,3=intr
    uint16_t wMaxPacketSize;    // Max packet size
    uint8_t  bInterval;         // Polling interval
                                //   FS/LS intr: 1–255 ms
                                //   HS intr:    2^(n-1) × 125µs, n=1–16
} __attribute__((packed)) usb_endpoint_descriptor_t;
_Static_assert(sizeof(usb_endpoint_descriptor_t) == 7,
               "usb_endpoint_descriptor_t must be 7 bytes");

// HID Descriptor — HID Spec Section 6.2.1
// Follows the Interface Descriptor for a HID interface.
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;   // = USB_HID_DESC_TYPE_HID (0x21)
    uint16_t bcdHID;            // HID spec version (BCD, e.g. 0x0111 = 1.11)
    uint8_t  bCountryCode;      // Hardware target country (0 = not localised)
    uint8_t  bNumDescriptors;   // Number of class descriptors (≥1)
    uint8_t  bDescriptorType2;  // Descriptor type of first class descriptor (0x22 = report)
    uint16_t wDescriptorLength; // Total length of the first class descriptor
} __attribute__((packed)) usb_hid_descriptor_t;
_Static_assert(sizeof(usb_hid_descriptor_t) == 9,
               "usb_hid_descriptor_t must be 9 bytes");

#define USB_HID_DESC_TYPE_HID    0x21
#define USB_HID_DESC_TYPE_REPORT 0x22
#define USB_HID_DESC_TYPE_PHYS   0x23

#define USB_CLASS_HID                  0x03
#define USB_HID_SUBCLASS_NONE          0x00
#define USB_HID_SUBCLASS_BOOT          0x01  // Supports Boot Protocol
#define USB_HID_PROTOCOL_NONE          0x00
#define USB_HID_PROTOCOL_KEYBOARD      0x01
#define USB_HID_PROTOCOL_MOUSE         0x02

#define USB_HID_REQTYPE_OUT  (USB_REQTYPE_DIR_OUT | USB_REQTYPE_TYPE_CLASS | USB_REQTYPE_RECIP_INTERFACE)
#define USB_HID_REQTYPE_IN   (USB_REQTYPE_DIR_IN  | USB_REQTYPE_TYPE_CLASS | USB_REQTYPE_RECIP_INTERFACE)

#define USB_HID_REQ_GET_REPORT    0x01  // Read a HID report
#define USB_HID_REQ_GET_IDLE      0x02  // Read the current idle rate
#define USB_HID_REQ_GET_PROTOCOL  0x03  // Read the current protocol (boot/report)
#define USB_HID_REQ_SET_REPORT    0x09  // Write a HID report (e.g. LED state)
#define USB_HID_REQ_SET_IDLE      0x0A  // Set the idle rate
#define USB_HID_REQ_SET_PROTOCOL  0x0B  // Switch between Boot and Report Protocol

// SET_PROTOCOL wValue field
#define USB_HID_PROTOCOL_BOOT_VAL   0   // Boot Protocol (simple fixed format)
#define USB_HID_PROTOCOL_REPORT_VAL 1   // Report Protocol (descriptor-driven)

// SET_IDLE wValue field:  high byte = duration (0=only on change), low byte = report ID
#define USB_HID_IDLE_INDEFINITE  0x00  // Only send reports when inputs change

// SET_REPORT / GET_REPORT wValue high byte = report type
#define USB_HID_REPORT_TYPE_INPUT   1
#define USB_HID_REPORT_TYPE_OUTPUT  2
#define USB_HID_REPORT_TYPE_FEATURE 3

typedef struct {
    // Modifier keys — each bit is one modifier key
    union {
        struct {
            uint8_t left_ctrl   : 1;
            uint8_t left_shift  : 1;
            uint8_t left_alt    : 1;
            uint8_t left_gui    : 1;  // Super key
            uint8_t right_ctrl  : 1;
            uint8_t right_shift : 1;
            uint8_t right_alt   : 1;
            uint8_t right_gui   : 1;
        };
        uint8_t modifiers;
    };

    uint8_t reserved;       // Always 0

    // Up to 6 simultaneously held key codes.
    // 0x00 = no key / empty slot.
    // 0x01 = keyboard error (rollover / phantom).
    uint8_t keycodes[6];
} __attribute__((packed)) hid_keyboard_report_t;
_Static_assert(sizeof(hid_keyboard_report_t) == 8,
               "hid_keyboard_report_t must be 8 bytes");

typedef struct {
    union {
        struct {
            uint8_t num_lock    : 1;
            uint8_t caps_lock   : 1;
            uint8_t scroll_lock : 1;
            uint8_t compose     : 1;
            uint8_t kana        : 1;
            uint8_t padding     : 3;
        };
        uint8_t raw;
    };
} __attribute__((packed)) hid_keyboard_leds_t;

#define HID_KEY_NONE         0x00
#define HID_KEY_ERR_ROLLOVER 0x01
#define HID_KEY_A            0x04  // a / A
#define HID_KEY_Z            0x1D  // z / Z  (A+25)
#define HID_KEY_1            0x1E  // 1 / !
#define HID_KEY_0            0x27  // 0 / )
#define HID_KEY_ENTER        0x28
#define HID_KEY_ESCAPE       0x29
#define HID_KEY_BACKSPACE    0x2A
#define HID_KEY_TAB          0x2B
#define HID_KEY_SPACE        0x2C
#define HID_KEY_MINUS        0x2D  // - / _
#define HID_KEY_EQUALS       0x2E  // = / +
#define HID_KEY_LBRACKET     0x2F  // [ / {
#define HID_KEY_RBRACKET     0x30  // ] / }
#define HID_KEY_BACKSLASH    0x31  // \ / |
#define HID_KEY_SEMICOLON    0x33  // ; / :
#define HID_KEY_APOSTROPHE   0x34  // ' / "
#define HID_KEY_GRAVE        0x35  // ` / ~
#define HID_KEY_COMMA        0x36  // , / <
#define HID_KEY_DOT          0x37  // . / >
#define HID_KEY_SLASH        0x38  // / / ?
#define HID_KEY_CAPS_LOCK    0x39
#define HID_KEY_F1           0x3A
#define HID_KEY_F12          0x45
#define HID_KEY_INSERT       0x49
#define HID_KEY_HOME         0x4A
#define HID_KEY_PAGE_UP      0x4B
#define HID_KEY_DELETE       0x4C
#define HID_KEY_END          0x4D
#define HID_KEY_PAGE_DOWN    0x4E
#define HID_KEY_RIGHT        0x4F
#define HID_KEY_LEFT         0x50
#define HID_KEY_DOWN         0x51
#define HID_KEY_UP           0x52
#define HID_KEY_KP_SLASH     0x54  // Keypad /
#define HID_KEY_KP_STAR      0x55  // Keypad *
#define HID_KEY_KP_MINUS     0x56  // Keypad -
#define HID_KEY_KP_PLUS      0x57  // Keypad +
#define HID_KEY_KP_ENTER     0x58  // Keypad Enter
#define HID_KEY_KP_1         0x59  // Keypad 1 / End
#define HID_KEY_KP_0         0x62  // Keypad 0 / Insert
#define HID_KEY_KP_DOT       0x63  // Keypad . / Delete

// Modifier bitmasks (for hid_keyboard_report_t.modifiers)
#define HID_MOD_LEFT_CTRL   (1 << 0)
#define HID_MOD_LEFT_SHIFT  (1 << 1)
#define HID_MOD_LEFT_ALT    (1 << 2)
#define HID_MOD_LEFT_GUI    (1 << 3)
#define HID_MOD_RIGHT_CTRL  (1 << 4)
#define HID_MOD_RIGHT_SHIFT (1 << 5)
#define HID_MOD_RIGHT_ALT   (1 << 6)
#define HID_MOD_RIGHT_GUI   (1 << 7)

#define HID_MOD_SHIFT  (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)
#define HID_MOD_CTRL   (HID_MOD_LEFT_CTRL  | HID_MOD_RIGHT_CTRL)
#define HID_MOD_ALT    (HID_MOD_LEFT_ALT   | HID_MOD_RIGHT_ALT)

#define HID_SPECIAL_KEY_NONE       0x0000  // No new key
#define HID_SPECIAL_KEY_F1         0x0100
#define HID_SPECIAL_KEY_F2         0x0101
#define HID_SPECIAL_KEY_F3         0x0102
#define HID_SPECIAL_KEY_F4         0x0103
#define HID_SPECIAL_KEY_F5         0x0104
#define HID_SPECIAL_KEY_F6         0x0105
#define HID_SPECIAL_KEY_F7         0x0106
#define HID_SPECIAL_KEY_F8         0x0107
#define HID_SPECIAL_KEY_F9         0x0108
#define HID_SPECIAL_KEY_F10        0x0109
#define HID_SPECIAL_KEY_F11        0x010A
#define HID_SPECIAL_KEY_F12        0x010B
#define HID_SPECIAL_KEY_UP         0x0200
#define HID_SPECIAL_KEY_DOWN       0x0201
#define HID_SPECIAL_KEY_LEFT       0x0202
#define HID_SPECIAL_KEY_RIGHT      0x0203
#define HID_SPECIAL_KEY_HOME       0x0204
#define HID_SPECIAL_KEY_END        0x0205
#define HID_SPECIAL_KEY_PAGE_UP    0x0206
#define HID_SPECIAL_KEY_PAGE_DOWN  0x0207
#define HID_SPECIAL_KEY_INSERT     0x0208
#define HID_SPECIAL_KEY_DELETE     0x007F  // Maps to ASCII DEL
#define HID_SPECIAL_KEY_CAPS_LOCK  0x0300

#endif // USB_HID_H