#include "framebuffer.h"
#include <mem/vmm.h>
#include <stdint.h>
#include <string.h>
#include <debug.h>

#define FB_MODULE "FB"

static uint8_t *fb_addr = 0;        // Virtual address for accessing framebuffer
static void *fb_phys_addr = 0;      // Physical address from Limine
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;
static uint16_t fb_bpp = 0;
static uint64_t fb_size = 0;        // Total framebuffer size in bytes

void fb_init(struct limine_framebuffer_response *response) {
    if (!response || response->framebuffer_count == 0) {
        log_err(FB_MODULE, "No framebuffer available");
        return;
    }

    struct limine_framebuffer *fb = response->framebuffers[0];
    
    fb_width  = fb->width;
    fb_height = fb->height;
    fb_pitch  = fb->pitch;
    fb_bpp    = fb->bpp;
    
    // Calculate total framebuffer size
    fb_size = fb_pitch * fb_height;
    
    log_info(FB_MODULE, "Framebuffer: %ux%u, %u bpp, pitch=%u",
             fb_width, fb_height, fb_bpp, fb_pitch);
    log_info(FB_MODULE, "Framebuffer size: %llu bytes (%llu KB)",
             fb_size, fb_size / 1024);
    
    // Store physical address
    fb_phys_addr = (void*)fb->address;
    log_info(FB_MODULE, "Physical address: 0x%llx", (uint64_t)fb_phys_addr);
    
    // Check if address is already in higher half (Limine might give us virtual address)
    if ((uint64_t)fb->address >= 0xFFFF800000000000ULL) {
        // Already a virtual address, use it directly
        fb_addr = (uint8_t*)fb->address;
        log_info(FB_MODULE, "Using virtual address directly: 0x%llx", (uint64_t)fb_addr);
    } else {
        // Physical address - need to identity map it
        fb_addr = (uint8_t*)fb->address;  // Use same address (identity map)
        
        // Calculate number of pages needed
        uint64_t pages_needed = (fb_size + PAGE_SIZE - 1) / PAGE_SIZE;
        log_info(FB_MODULE, "Identity mapping %llu pages for framebuffer", pages_needed);
        
        // Identity map the framebuffer region
        // Use PAGE_WRITE | PAGE_PRESENT | PAGE_NOCACHE for framebuffer
        // NOCACHE is important for memory-mapped video memory
        if (!vmm_map_range(vmm_get_kernel_space(), 
                          fb_addr, 
                          fb_phys_addr, 
                          pages_needed,
                          PAGE_WRITE | PAGE_PRESENT | PAGE_NOCACHE)) {
            log_crit(FB_MODULE, "Failed to map framebuffer memory");
            fb_addr = 0;
            return;
        }
        
        log_ok(FB_MODULE, "Framebuffer mapped at 0x%llx", (uint64_t)fb_addr);
    }
    
    log_ok(FB_MODULE, "Framebuffer initialized successfully");
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_addr) {
        return;
    }
    
    if (x >= fb_width || y >= fb_height) {
        return;
    }

    uint8_t *pixel = fb_addr + y * fb_pitch + x * (fb_bpp / 8);
    
    if (fb_bpp == 32) {
        *(uint32_t*)pixel = color;
    } else if (fb_bpp == 24) {
        pixel[0] = color & 0xFF;
        pixel[1] = (color >> 8) & 0xFF;
        pixel[2] = (color >> 16) & 0xFF;
    }
}

void fb_clear(uint32_t color) {
    if (!fb_addr) {
        return;
    }
    
    if (fb_bpp != 32) {
        log_warn(FB_MODULE, "fb_clear only supports 32bpp");
        return;
    }

    uint32_t *fb32 = (uint32_t*)fb_addr;
    uint32_t pixels_per_row = fb_pitch / 4;
    
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            fb32[y * pixels_per_row + x] = color;
        }
    }
}

void fb_scroll(uint32_t pixels, uint32_t bg_color) {
    if (!fb_addr) {
        log_warn(FB_MODULE, "fb_scroll called but framebuffer not initialized");
        return;
    }
    
    if (fb_bpp != 32) {
        log_warn(FB_MODULE, "fb_scroll only supports 32bpp");
        return;
    }
    
    if (pixels == 0) {
        return;
    }
    
    if (pixels >= fb_height) {
        // Scrolling entire screen, just clear it
        fb_clear(bg_color);
        return;
    }

    uint32_t row_size = fb_pitch;
    uint32_t scroll_bytes = pixels * row_size;
    uint32_t remaining_bytes = (fb_height - pixels) * row_size;
    
    // Validate that we're not going out of bounds
    if (scroll_bytes > fb_size || remaining_bytes > fb_size) {
        log_err(FB_MODULE, "Scroll would exceed framebuffer bounds");
        return;
    }
    
    // Move framebuffer memory up
    // Use memmove to handle overlapping regions safely
    memmove(
        fb_addr,                          // destination
        fb_addr + scroll_bytes,          // source
        remaining_bytes                   // size
    );
    
    // Clear bottom area that was scrolled away
    uint8_t *bottom = fb_addr + remaining_bytes;
    uint32_t clear_bytes = scroll_bytes;
    
    // Validate clear range
    if ((uint64_t)(bottom - fb_addr) + clear_bytes > fb_size) {
        log_err(FB_MODULE, "Clear would exceed framebuffer bounds");
        return;
    }
    
    // Clear using 32-bit writes for efficiency
    uint32_t *bottom32 = (uint32_t*)bottom;
    uint32_t pixels_to_clear = clear_bytes / 4;
    
    for (uint32_t i = 0; i < pixels_to_clear; i++) {
        bottom32[i] = bg_color;
    }
}

uint32_t fb_get_width(void)  { 
    return fb_width; 
}

uint32_t fb_get_height(void) { 
    return fb_height; 
}

uint32_t fb_get_pitch(void)  { 
    return fb_pitch; 
}