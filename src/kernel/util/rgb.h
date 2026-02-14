#pragma once
#include <stdint.h>

/**
 * Converts 8-bit RGB values into a single 32-bit framebuffer color.
 * Format: 0x00RRGGBB
 */
static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
