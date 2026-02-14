#ifndef FONTLOADER_H
#define FONTLOADER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t width;
    uint8_t height;
    uint16_t num_glyphs;
    const uint8_t *glyph_data;
} font_t;

// Load a font from a Limine module
// Returns true on success, false on failure
bool font_load(const char *module_name);

// Get the currently loaded font (or default if none loaded)
const font_t* font_get_current(void);

// Get a pointer to a specific glyph
const uint8_t* font_get_glyph(unsigned char c);

#endif // FONTLOADER_H