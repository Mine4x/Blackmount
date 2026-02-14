#include "fontloader.h"
#include "std_font.h"
#include "debug.h"
#include <limine/limine_req.h>
#include <stdbool.h>
#include <stdint.h>

// Default font structure using std_font
static font_t default_font = {
    .width = 8,
    .height = 8,
    .num_glyphs = 128,
    .glyph_data = (const uint8_t*)std_font
};

static font_t current_font;
static bool custom_font_loaded = false;

static uint8_t font_buffer[256 * 16];

static int parse_int(const char *str, int *out) {
    int val = 0;
    int sign = 1;
    int i = 0;
    
    while (str[i] == ' ' || str[i] == '\t') i++;
    
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }
    
    if (str[i] < '0' || str[i] > '9') return 0; // No digits
    
    while (str[i] >= '0' && str[i] <= '9') {
        val = val * 10 + (str[i] - '0');
        i++;
    }
    
    *out = val * sign;
    return i;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static uint8_t parse_hex_byte(const char *str) {
    int high = hex_digit(str[0]);
    int low = hex_digit(str[1]);
    if (high < 0 || low < 0) return 0;
    return (high << 4) | low;
}

static bool starts_with(const char *line, const char *prefix) {
    while (*prefix) {
        if (*line != *prefix) return false;
        line++;
        prefix++;
    }
    return true;
}

static const char* next_line(const char *ptr, const char *end) {
    while (ptr < end && *ptr != '\n') ptr++;
    if (ptr < end && *ptr == '\n') ptr++;
    return ptr;
}

void font_init(void) {
    log_info("Fonts", "Initializing font system with default font");
    current_font = default_font;
    custom_font_loaded = false;
    log_debug("Fonts", "Default font: %dx%d, %d glyphs", 
              default_font.width, default_font.height, default_font.num_glyphs);
}

bool font_load(const char *module_name) {
    log_info("Fonts", "Attempting to load BDF font module: %s", module_name);
    
    uint64_t size;
    const char *buffer = (const char*)limine_get_module(module_name, &size);
    
    if (!buffer) {
        log_err("Fonts", "Module '%s' not found", module_name);
        return false;
    }
    
    log_debug("Fonts", "Module loaded, size: %d bytes", (uint32_t)size);
    
    const char *ptr = buffer;
    const char *end = buffer + size;
    
    // Parse BDF header
    if (!starts_with(ptr, "STARTFONT")) {
        log_err("Fonts", "Not a valid BDF file (missing STARTFONT)");
        return false;
    }
    
    log_debug("Fonts", "BDF header found");
    
    int font_width = 0;
    int font_height = 0;
    int font_ascent = 0;
    int font_descent = 0;
    int default_char = -1;
    
    // Clear font buffer
    for (int i = 0; i < sizeof(font_buffer); i++) {
        font_buffer[i] = 0;
    }
    
    int glyph_count = 0;
    
    // Parse BDF file
    while (ptr < end) {
        // Find start of line
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ptr++;
        
        if (starts_with(ptr, "FONTBOUNDINGBOX")) {
            ptr += 15; // strlen("FONTBOUNDINGBOX")
            while (*ptr == ' ') ptr++;
            
            int w, h, xoff, yoff;
            ptr += parse_int(ptr, &w);
            while (*ptr == ' ') ptr++;
            ptr += parse_int(ptr, &h);
            while (*ptr == ' ') ptr++;
            ptr += parse_int(ptr, &xoff);
            while (*ptr == ' ') ptr++;
            ptr += parse_int(ptr, &yoff);
            
            font_width = w;
            font_height = h;
            
            log_debug("Fonts", "FONTBOUNDINGBOX: %dx%d (offset: %d,%d)", 
                     w, h, xoff, yoff);
        }
        else if (starts_with(ptr, "FONT_ASCENT")) {
            ptr += 11;
            while (*ptr == ' ') ptr++;
            ptr += parse_int(ptr, &font_ascent);
            log_debug("Fonts", "FONT_ASCENT: %d", font_ascent);
        }
        else if (starts_with(ptr, "FONT_DESCENT")) {
            ptr += 12;
            while (*ptr == ' ') ptr++;
            ptr += parse_int(ptr, &font_descent);
            log_debug("Fonts", "FONT_DESCENT: %d", font_descent);
        }
        else if (starts_with(ptr, "DEFAULT_CHAR")) {
            ptr += 12;
            while (*ptr == ' ') ptr++;
            ptr += parse_int(ptr, &default_char);
            log_debug("Fonts", "DEFAULT_CHAR: %d", default_char);
        }
        else if (starts_with(ptr, "STARTCHAR")) {
            // Parse character
            ptr = next_line(ptr, end);
            
            if (!starts_with(ptr, "ENCODING")) {
                ptr = next_line(ptr, end);
                continue;
            }
            
            ptr += 8; // strlen("ENCODING")
            while (*ptr == ' ') ptr++;
            
            int encoding;
            ptr += parse_int(ptr, &encoding);
            ptr = next_line(ptr, end);
            
            // Skip to BITMAP
            while (ptr < end && !starts_with(ptr, "BITMAP")) {
                ptr = next_line(ptr, end);
            }
            
            if (ptr >= end) break;
            
            ptr = next_line(ptr, end); // Skip BITMAP line
            
            // Parse bitmap data
            if (encoding >= 0 && encoding < 256) {
                int row = 0;
                while (ptr < end && !starts_with(ptr, "ENDCHAR") && row < 16) {
                    // Skip whitespace
                    while (*ptr == ' ' || *ptr == '\t') ptr++;
                    
                    // Parse hex data
                    if (hex_digit(ptr[0]) >= 0) {
                        uint8_t byte = parse_hex_byte(ptr);
                        
                        // Store in font buffer
                        if (row < font_height) {
                            font_buffer[encoding * 16 + row] = byte;
                        }
                        row++;
                    }
                    
                    ptr = next_line(ptr, end);
                }
                
                if (encoding >= glyph_count) {
                    glyph_count = encoding + 1;
                }
            }
        }
        
        ptr = next_line(ptr, end);
    }
    
    // Validate parsed font
    if (font_width == 0 || font_height == 0) {
        log_err("Fonts", "Invalid BDF: width=%d, height=%d", font_width, font_height);
        return false;
    }
    
    if (font_height > 16) {
        log_err("Fonts", "Font height too large: %d (max 16)", font_height);
        return false;
    }
    
    if (glyph_count == 0) {
        log_err("Fonts", "No glyphs found in BDF file");
        return false;
    }
    
    // Update current font
    current_font.width = font_width;
    current_font.height = font_height;
    current_font.num_glyphs = glyph_count;
    current_font.glyph_data = font_buffer;
    custom_font_loaded = true;
    
    log_ok("Fonts", "BDF font loaded: %dx%d, %d glyphs", 
           font_width, font_height, glyph_count);
    
    return true;
}

const font_t* font_get_current(void) {
    return &current_font;
}

const uint8_t* font_get_glyph(unsigned char c) {
    // Use default character if out of range
    if (c >= current_font.num_glyphs) {
        c = '?';
        if (c >= current_font.num_glyphs) {
            c = 0; // Use first glyph as fallback
        }
    }
    
    return &current_font.glyph_data[c * 16];
}