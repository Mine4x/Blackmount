#include <drivers/input/keyboard/ps2.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/irq.h>
#include <debug.h>

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Scancode Set 1 translation table
static const char scancode_lowercase[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_uppercase[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Special key scancodes
#define KEY_LSHIFT 0x2A
#define KEY_RSHIFT 0x36
#define KEY_LCTRL 0x1D
#define KEY_LALT 0x38
#define KEY_CAPSLOCK 0x3A

// Keyboard state
static struct {
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;
    bool capslock_on;
} keyboard_state = {0};

// Keyboard buffer (circular buffer)
#define KEYBOARD_BUFFER_SIZE 256
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buffer_write_pos = 0;
static volatile uint32_t buffer_read_pos = 0;

void (*callback)();

// Add character to buffer
static void keyboard_buffer_push(char c) {
    uint32_t next_pos = (buffer_write_pos + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_pos != buffer_read_pos) {
        keyboard_buffer[buffer_write_pos] = c;
        buffer_write_pos = next_pos;
    }
}

// IRQ handler for keyboard (IRQ1)
static void keyboard_irq_handler(Registers* regs) {
    uint8_t scancode = x86_64_inb(KEYBOARD_DATA_PORT);
    
    // Check if this is a key release (bit 7 set)
    bool key_released = (scancode & 0x80) != 0;
    scancode &= 0x7F; // Remove the release bit
    
    // Handle special keys
    if (scancode == KEY_LSHIFT || scancode == KEY_RSHIFT) {
        keyboard_state.shift_pressed = !key_released;
        return;
    }
    
    if (scancode == KEY_LCTRL) {
        keyboard_state.ctrl_pressed = !key_released;
        return;
    }
    
    if (scancode == KEY_LALT) {
        keyboard_state.alt_pressed = !key_released;
        return;
    }
    
    if (scancode == KEY_CAPSLOCK && !key_released) {
        keyboard_state.capslock_on = !keyboard_state.capslock_on;
        return;
    }
    
    // Only process key press events for regular keys
    if (key_released) {
        return;
    }
    
    // Translate scancode to character
    char c = 0;
    bool uppercase = keyboard_state.shift_pressed ^ keyboard_state.capslock_on;
    
    if (scancode < 128) {
        c = uppercase ? scancode_uppercase[scancode] : scancode_lowercase[scancode];
    }
    
    // Add to buffer if it's a valid character
    if (c != 0) {
        keyboard_buffer_push(c);
        callback(c);
    }
}

// Get character from buffer (returns 0 if empty)
char ps2_keyboard_getchar(void) {
    if (buffer_read_pos == buffer_write_pos) {
        return 0; // Buffer empty
    }
    char c = keyboard_buffer[buffer_read_pos];
    buffer_read_pos = (buffer_read_pos + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

// Check if buffer has data
bool ps2_keyboard_has_input(void) {
    return buffer_read_pos != buffer_write_pos;
}

// Initialize keyboard
void ps2_keyboard_init(void) {
    log_info("PS2", "Called PS2 init");
    // Clear keyboard state
    keyboard_state.shift_pressed = false;
    keyboard_state.ctrl_pressed = false;
    keyboard_state.alt_pressed = false;
    keyboard_state.capslock_on = false;
    
    buffer_write_pos = 0;
    buffer_read_pos = 0;
    
    // Register IRQ1 handler for keyboard
    x86_64_IRQ_RegisterHandler(1, keyboard_irq_handler);
    x86_64_IRQ_Unmask(1);
}

void ps2_keyboard_bind(void (*ptr)()) {
    callback = ptr;
}