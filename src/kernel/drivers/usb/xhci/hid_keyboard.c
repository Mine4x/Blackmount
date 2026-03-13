#include "hid_keyboard.h"
#include "usb_hid.h"
#include "xhci.h"
#include "xhci_mem.h"
#include "xhci_common.h"
#include <debug.h>
#include <memory.h>

#define HID_MAX_KEYBOARDS 4

static hid_keyboard_device_t m_keyboards[HID_MAX_KEYBOARDS];
static int                    m_keyboard_count = 0;
static int                    m_active_kbd_idx = 0; // Which keyboard read_key reads from

static void _probe(xhci_controller_t* hc, xhci_device_t* dev);
static int  _find_hid_keyboard_interface(uint8_t* cfg_buf, uint16_t total_len,
                                          uint8_t* out_iface_num,
                                          uint8_t* out_config_val,
                                          uint8_t* out_ep_addr,
                                          uint8_t* out_ep_attrs,
                                          uint16_t* out_ep_max_pkt,
                                          uint8_t* out_ep_interval);
static int  _read_full_config_descriptor(xhci_controller_t* hc, xhci_device_t* dev,
                                          uint8_t** buf_out, uint16_t* len_out);
static int  _set_configuration(xhci_controller_t* hc, xhci_device_t* dev, uint8_t config_val);
static int  _set_protocol(xhci_controller_t* hc, xhci_device_t* dev,
                           uint8_t iface_num, uint8_t protocol);
static int  _set_idle(xhci_controller_t* hc, xhci_device_t* dev,
                      uint8_t iface_num, uint8_t duration, uint8_t report_id);
static int  _set_leds_raw(hid_keyboard_device_t* kbd, hid_keyboard_leds_t leds);
static uint16_t _keycode_to_char(uint8_t keycode, uint8_t modifiers, bool caps_lock);
static bool _keycode_in_report(const hid_keyboard_report_t* report, uint8_t code);

static const uint8_t _keymap_normal[128] = {
//  0     1     2     3     4     5     6     7     8     9
    0,    0,    0,    0,   'a', 'b',  'c',  'd',  'e',  'f',  // 0x00–0x09
   'g',  'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',  'p', // 0x0A–0x13
   'q',  'r',  's',  't',  'u',  'v',  'w',  'x',  'y',  'z', // 0x14–0x1D
   '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0', // 0x1E–0x27
   '\n', '\x1B', '\b', '\t', ' ',  '-',  '=',  '[',  ']', '\\', // 0x28–0x31
    0,   ';',  '\'', '`',  ',',  '.',  '/',   0,    0,    0,   // 0x32–0x3B
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 0x3C–0x45
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 0x46–0x4F (nav keys below)
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 0x50–0x59
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 0x5A–0x63
    0,    0,    0,    0,    0,    0,    0,    0,                 // 0x64–0x6B
};

static const uint8_t _keymap_shifted[128] = {
//  0     1     2     3     4     5     6     7     8     9
    0,    0,    0,    0,   'A', 'B',  'C',  'D',  'E',  'F',  // 0x00–0x09
   'G',  'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',  'P', // 0x0A–0x13
   'Q',  'R',  'S',  'T',  'U',  'V',  'W',  'X',  'Y',  'Z', // 0x14–0x1D
   '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')', // 0x1E–0x27
   '\n', '\x1B', '\b', '\t', ' ',  '_',  '+',  '{',  '}', '|', // 0x28–0x31
    0,   ':',  '"',  '~',  '<',  '>',  '?',   0,    0,    0,  // 0x32–0x3B
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 0x3C–0x45
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 0x46–0x4F
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 0x50–0x59
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 0x5A–0x63
    0,    0,    0,    0,    0,    0,    0,    0,                 // 0x64–0x6B
};

void hid_keyboard_init()
{
    memset(m_keyboards, 0, sizeof(m_keyboards));
    m_keyboard_count = 0;
    xhci_register_probe_callback(_probe);
    log_info(HID_KEYBOARD_MOD, "HID keyboard driver registered");
}

uint16_t hid_keyboard_read_key()
{
    if (m_keyboard_count == 0)
        return HID_SPECIAL_KEY_NONE;

    hid_keyboard_device_t* kbd = &m_keyboards[m_active_kbd_idx];

    if (!kbd->active)
        return HID_SPECIAL_KEY_NONE;

    for (;;) {

        if (xhci_wait_transfer(kbd->dev,
                               kbd->intr_ep_addr,
                               100) < 0)
            continue;

        uint8_t dci = xhci_dci_from_ep_addr(kbd->intr_ep_addr);
        kbd->dev->ep_transfer_completed[dci] = false;

        hid_keyboard_report_t* r =
            (hid_keyboard_report_t*)kbd->report_buf;

        if (r->keycodes[0] == HID_KEY_ERR_ROLLOVER)
            continue;

        if (_keycode_in_report(r, HID_KEY_CAPS_LOCK) &&
            !_keycode_in_report(&kbd->prev_report, HID_KEY_CAPS_LOCK)) {

            kbd->caps_lock_active = !kbd->caps_lock_active;
        }

        kbd->modifiers = r->modifiers;

        uint16_t result = HID_SPECIAL_KEY_NONE;

        for (int i = 0; i < 6; i++) {

            uint8_t code = r->keycodes[i];

            if (code == HID_KEY_NONE)
                continue;

            if (_keycode_in_report(&kbd->prev_report, code))
                continue;

            result = _keycode_to_char(code,
                                      r->modifiers,
                                      kbd->caps_lock_active);

            if (result != HID_SPECIAL_KEY_NONE)
                break;
        }

        kbd->prev_report = *r;

        xhci_queue_transfer(
            kbd->hc,
            kbd->dev,
            kbd->intr_ep_addr,
            kbd->report_buf,
            kbd->intr_ep_max_pkt);

        if (result != HID_SPECIAL_KEY_NONE)
            return result;
    }
}

bool hid_keyboard_key_available()
{
    if (m_keyboard_count == 0) return false;
    hid_keyboard_device_t* kbd = &m_keyboards[m_active_kbd_idx];
    if (!kbd->active) return false;
    uint8_t dci = xhci_dci_from_ep_addr(kbd->intr_ep_addr);
    return (bool)kbd->dev->ep_transfer_completed[dci];
}

bool hid_keyboard_has_error()
{
    if (m_keyboard_count == 0) return false;
    return m_keyboards[m_active_kbd_idx].error;
}

void hid_keyboard_set_leds(hid_keyboard_leds_t leds)
{
    if (m_keyboard_count == 0) return;
    _set_leds_raw(&m_keyboards[m_active_kbd_idx], leds);
}

static void _probe(xhci_controller_t* hc, xhci_device_t* dev)
{
    if (m_keyboard_count >= HID_MAX_KEYBOARDS)
        return;

    usb_device_descriptor_t* dd = &dev->descriptor;

    /* Only consider HID devices or composite devices */
    if (dd->bDeviceClass != USB_CLASS_HID && dd->bDeviceClass != 0x00)
        return;

    log_debug(HID_KEYBOARD_MOD,
        "Probing device VID=%04x PID=%04x class=%02x",
        dd->idVendor, dd->idProduct, dd->bDeviceClass);

    uint8_t*  cfg_buf = NULL;
    uint16_t  cfg_len = 0;

    if (_read_full_config_descriptor(hc, dev, &cfg_buf, &cfg_len) < 0) {
        log_err(HID_KEYBOARD_MOD, "Failed to read configuration descriptor");
        return;
    }

    usb_config_descriptor_t* cfg =
        (usb_config_descriptor_t*)cfg_buf;

    if (_set_configuration(hc, dev, cfg->bConfigurationValue) < 0) {
        log_err(HID_KEYBOARD_MOD, "SET_CONFIGURATION failed");
        free_xhci_memory(cfg_buf);
        return;
    }

    uint8_t iface_num  = 0;
    uint8_t config_val = 0;
    uint8_t ep_addr    = 0;
    uint8_t ep_attrs   = 0;
    uint8_t ep_interval= 0;
    uint16_t ep_max_pkt= 0;

    int found = _find_hid_keyboard_interface(
        cfg_buf,
        cfg_len,
        &iface_num,
        &config_val,
        &ep_addr,
        &ep_attrs,
        &ep_max_pkt,
        &ep_interval);

    if (found < 0) {
        log_debug(HID_KEYBOARD_MOD,
            "No HID keyboard interface found");
        free_xhci_memory(cfg_buf);
        return;
    }

    log_info(HID_KEYBOARD_MOD,
        "HID keyboard interface=%u endpoint=0x%02x pkt=%u interval=%u",
        iface_num, ep_addr, ep_max_pkt, ep_interval);

    free_xhci_memory(cfg_buf);

    if (_set_protocol(hc, dev, iface_num,
        USB_HID_PROTOCOL_BOOT_VAL) < 0)
    {
        log_warn(HID_KEYBOARD_MOD,
            "SET_PROTOCOL failed (device may ignore)");
    }

    if (_set_idle(hc, dev, iface_num,
        USB_HID_IDLE_INDEFINITE, 0) < 0)
    {
        log_warn(HID_KEYBOARD_MOD,
            "SET_IDLE failed");
    }

    if (xhci_configure_endpoint(
            hc,
            dev,
            ep_addr,
            ep_attrs,
            ep_max_pkt,
            ep_interval) < 0)
    {
        log_err(HID_KEYBOARD_MOD,
            "Failed to configure keyboard endpoint");
        return;
    }

    uint8_t* report_buf =
        alloc_xhci_memory(ep_max_pkt, 64, PAGE_SIZE);

    if (!report_buf) {
        log_err(HID_KEYBOARD_MOD,
            "Failed to allocate report buffer");
        return;
    }

    memset(report_buf, 0, ep_max_pkt);

    hid_keyboard_device_t* kbd =
        &m_keyboards[m_keyboard_count];

    memset(kbd, 0, sizeof(*kbd));

    kbd->active           = true;
    kbd->hc               = hc;
    kbd->dev              = dev;
    kbd->intr_ep_addr     = ep_addr;
    kbd->intr_ep_attrs    = ep_attrs;
    kbd->intr_ep_max_pkt  = ep_max_pkt;
    kbd->intr_ep_interval = ep_interval;
    kbd->config_value     = config_val;
    kbd->interface_num    = iface_num;
    kbd->report_buf       = report_buf;

    if (xhci_queue_transfer(
            hc,
            dev,
            ep_addr,
            report_buf,
            ep_max_pkt) < 0)
    {
        log_err(HID_KEYBOARD_MOD,
            "Failed to queue keyboard interrupt");
        free_xhci_memory(report_buf);
        return;
    }

    m_keyboard_count++;

    log_ok(HID_KEYBOARD_MOD,
        "USB HID keyboard registered (index %d)",
        m_keyboard_count - 1);
}

static int _read_full_config_descriptor(xhci_controller_t* hc, xhci_device_t* dev,
                                         uint8_t** buf_out, uint16_t* len_out)
{ 
    void* hdr = alloc_xhci_memory(16, 64, PAGE_SIZE);
    if (!hdr) return -1;
    memset(hdr, 0, 16);

    usb_setup_packet_t setup = {
        .bmRequestType = USB_REQTYPE_DIR_IN | USB_REQTYPE_TYPE_STANDARD
                       | USB_REQTYPE_RECIP_DEVICE,
        .bRequest      = USB_REQ_GET_DESCRIPTOR,
        .wValue        = (uint16_t)((USB_DESC_TYPE_CONFIG << 8) | 0x00),
        .wIndex        = 0,
        .wLength       = sizeof(usb_config_descriptor_t)
    };

    if (xhci_control_transfer(hc, dev, &setup, hdr,
                              sizeof(usb_config_descriptor_t), true, 2000) < 0) {
        free_xhci_memory(hdr);
        return -1;
    }

    usb_config_descriptor_t* cfg_hdr = (usb_config_descriptor_t*)hdr;
    uint16_t total_len = cfg_hdr->wTotalLength;
    free_xhci_memory(hdr);

    if (total_len < sizeof(usb_config_descriptor_t) || total_len > 4096) {
        log_err(HID_KEYBOARD_MOD, "Suspicious wTotalLength=%d", total_len);
        return -1;
    }
    uint16_t alloc_len = (uint16_t)((total_len + 63) & ~63u);
    void* full = alloc_xhci_memory(alloc_len, 64, PAGE_SIZE);
    if (!full) return -1;
    memset(full, 0, alloc_len);

    setup.wLength = total_len;
    if (xhci_control_transfer(hc, dev, &setup, full, total_len, true, 2000) < 0) {
        free_xhci_memory(full);
        return -1;
    }

    *buf_out = (uint8_t*)full;
    *len_out = total_len;
    return 0;
}

static int _find_hid_keyboard_interface(uint8_t* buf, uint16_t total_len,
                                        uint8_t* out_iface_num,
                                        uint8_t* out_config_val,
                                        uint8_t* out_ep_addr,
                                        uint8_t* out_ep_attrs,
                                        uint16_t* out_ep_max_pkt,
                                        uint8_t* out_ep_interval)
{
    usb_config_descriptor_t* cfg = (usb_config_descriptor_t*)buf;
    *out_config_val = cfg->bConfigurationValue;

    uint8_t* p   = buf;
    uint8_t* end = buf + total_len;

    bool in_hid_keyboard_iface = false;
    uint8_t current_iface_num  = 0;

    while (p < end) {

        if (p + 2 > end)
            break;

        uint8_t bLength = p[0];
        uint8_t bType   = p[1];

        if (bLength == 0 || p + bLength > end)
            break;

        switch (bType) {

        case USB_DESC_TYPE_INTERFACE: {

            if (bLength < sizeof(usb_interface_descriptor_t))
                break;

            usb_interface_descriptor_t* iface =
                (usb_interface_descriptor_t*)p;

            log_debug(HID_KEYBOARD_MOD,
                "Interface %d: Class=%02x SubClass=%02x Protocol=%02x",
                iface->bInterfaceNumber,
                iface->bInterfaceClass,
                iface->bInterfaceSubClass,
                iface->bInterfaceProtocol);

            in_hid_keyboard_iface =
                (iface->bInterfaceClass    == USB_CLASS_HID &&
                 iface->bInterfaceProtocol == USB_HID_PROTOCOL_KEYBOARD);

            current_iface_num = iface->bInterfaceNumber;
            break;
        }

        case USB_DESC_TYPE_ENDPOINT: {

            if (!in_hid_keyboard_iface)
                break;

            if (bLength < sizeof(usb_endpoint_descriptor_t))
                break;

            usb_endpoint_descriptor_t* ep =
                (usb_endpoint_descriptor_t*)p;

            bool is_in   = (ep->bEndpointAddress & 0x80) != 0;
            bool is_intr = (ep->bmAttributes & 0x03) == 3;

            /* Prefer boot keyboard endpoint */
            if (is_in && is_intr && ep->wMaxPacketSize <= 8) {

                *out_iface_num   = current_iface_num;
                *out_ep_addr     = ep->bEndpointAddress;
                *out_ep_attrs    = ep->bmAttributes;
                *out_ep_max_pkt  = ep->wMaxPacketSize;
                *out_ep_interval = ep->bInterval;

                return 0;
            }

            break;
        }

        default:
            break;
        }

        p += bLength;
    }

    return -1;
}

static int _set_configuration(xhci_controller_t* hc, xhci_device_t* dev,
                               uint8_t config_val)
{
    usb_setup_packet_t setup = {
        .bmRequestType = USB_REQTYPE_DIR_OUT | USB_REQTYPE_TYPE_STANDARD
                       | USB_REQTYPE_RECIP_DEVICE,
        .bRequest      = USB_REQ_SET_CONFIGURATION,
        .wValue        = config_val,
        .wIndex        = 0,
        .wLength       = 0
    };
    return xhci_control_transfer(hc, dev, &setup, NULL, 0, false, 2000);
}

static int _set_protocol(xhci_controller_t* hc, xhci_device_t* dev,
                          uint8_t iface_num, uint8_t protocol)
{
    // HID Spec Section 7.2.5
    // bmRequestType=0x21 (Class|Interface|HostToDevice), bRequest=0x0B
    usb_setup_packet_t setup = {
        .bmRequestType = USB_HID_REQTYPE_OUT,
        .bRequest      = USB_HID_REQ_SET_PROTOCOL,
        .wValue        = protocol,    // 0=Boot, 1=Report
        .wIndex        = iface_num,
        .wLength       = 0
    };
    return xhci_control_transfer(hc, dev, &setup, NULL, 0, false, 2000);
}

static int _set_idle(xhci_controller_t* hc, xhci_device_t* dev,
                     uint8_t iface_num, uint8_t duration, uint8_t report_id)
{
    // HID Spec Section 7.2.4
    // wValue: high byte = duration (0 = only on change), low byte = report ID
    usb_setup_packet_t setup = {
        .bmRequestType = USB_HID_REQTYPE_OUT,
        .bRequest      = USB_HID_REQ_SET_IDLE,
        .wValue        = (uint16_t)((duration << 8) | report_id),
        .wIndex        = iface_num,
        .wLength       = 0
    };
    return xhci_control_transfer(hc, dev, &setup, NULL, 0, false, 2000);
}

static int _set_leds_raw(hid_keyboard_device_t* kbd, hid_keyboard_leds_t leds)
{
    // Allocate a DMA-accessible buffer for the 1-byte output report.
    uint8_t* buf = (uint8_t*)alloc_xhci_memory(4, 4, PAGE_SIZE);
    if (!buf) return -1;
    buf[0] = leds.raw;

    usb_setup_packet_t setup = {
        .bmRequestType = USB_HID_REQTYPE_OUT,
        .bRequest      = USB_HID_REQ_SET_REPORT,
        // wValue: high byte = report type (2=Output), low byte = report ID (0)
        .wValue        = (uint16_t)((USB_HID_REPORT_TYPE_OUTPUT << 8) | 0x00),
        .wIndex        = kbd->interface_num,
        .wLength       = 1
    };

    int ret = xhci_control_transfer(kbd->hc, kbd->dev, &setup, buf, 1, false, 2000);
    free_xhci_memory(buf);
    return ret;
}

static bool _keycode_in_report(const hid_keyboard_report_t* report, uint8_t code)
{
    for (int i = 0; i < 6; i++)
        if (report->keycodes[i] == code) return true;
    return false;
}

static uint16_t _keycode_to_char(uint8_t keycode, uint8_t modifiers, bool caps_lock)
{
    bool shift = (modifiers & HID_MOD_SHIFT) != 0;

    bool is_alpha = (keycode >= HID_KEY_A && keycode <= HID_KEY_Z);
    bool upper    = shift ^ (caps_lock && is_alpha);

    if (keycode < 128) {
        uint8_t c = upper ? _keymap_shifted[keycode] : _keymap_normal[keycode];
        if (c) {
            if ((modifiers & HID_MOD_CTRL) && c >= 'a' && c <= 'z')
                return (uint16_t)(c - 'a' + 1);
            if ((modifiers & HID_MOD_CTRL) && c >= 'A' && c <= 'Z')
                return (uint16_t)(c - 'A' + 1);
            return (uint16_t)c;
        }
    }

    switch (keycode) {
    case HID_KEY_F1:        return HID_SPECIAL_KEY_F1;
    case HID_KEY_F1 + 1:   return HID_SPECIAL_KEY_F2;
    case HID_KEY_F1 + 2:   return HID_SPECIAL_KEY_F3;
    case HID_KEY_F1 + 3:   return HID_SPECIAL_KEY_F4;
    case HID_KEY_F1 + 4:   return HID_SPECIAL_KEY_F5;
    case HID_KEY_F1 + 5:   return HID_SPECIAL_KEY_F6;
    case HID_KEY_F1 + 6:   return HID_SPECIAL_KEY_F7;
    case HID_KEY_F1 + 7:   return HID_SPECIAL_KEY_F8;
    case HID_KEY_F1 + 8:   return HID_SPECIAL_KEY_F9;
    case HID_KEY_F1 + 9:   return HID_SPECIAL_KEY_F10;
    case HID_KEY_F1 + 10:  return HID_SPECIAL_KEY_F11;
    case HID_KEY_F12:      return HID_SPECIAL_KEY_F12;
    case HID_KEY_UP:       return HID_SPECIAL_KEY_UP;
    case HID_KEY_DOWN:     return HID_SPECIAL_KEY_DOWN;
    case HID_KEY_LEFT:     return HID_SPECIAL_KEY_LEFT;
    case HID_KEY_RIGHT:    return HID_SPECIAL_KEY_RIGHT;
    case HID_KEY_HOME:     return HID_SPECIAL_KEY_HOME;
    case HID_KEY_END:      return HID_SPECIAL_KEY_END;
    case HID_KEY_PAGE_UP:  return HID_SPECIAL_KEY_PAGE_UP;
    case HID_KEY_PAGE_DOWN:return HID_SPECIAL_KEY_PAGE_DOWN;
    case HID_KEY_INSERT:   return HID_SPECIAL_KEY_INSERT;
    case HID_KEY_DELETE:   return HID_SPECIAL_KEY_DELETE; // 0x7F (ASCII DEL)
    case HID_KEY_CAPS_LOCK:return HID_SPECIAL_KEY_CAPS_LOCK;

    // Keypad — treat like the normal digit/symbol equivalents
    case HID_KEY_KP_SLASH: return '/';
    case HID_KEY_KP_STAR:  return '*';
    case HID_KEY_KP_MINUS: return '-';
    case HID_KEY_KP_PLUS:  return '+';
    case HID_KEY_KP_ENTER: return '\r';
    case HID_KEY_KP_DOT:   return '.';
    default:
        // Keypad 1–9, 0
        if (keycode >= HID_KEY_KP_1 && keycode <= HID_KEY_KP_1 + 8)
            return (uint16_t)('1' + (keycode - HID_KEY_KP_1));
        if (keycode == HID_KEY_KP_0) return '0';
        break;
    }

    return HID_SPECIAL_KEY_NONE;
}