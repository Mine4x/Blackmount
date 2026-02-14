#include "pmm.h"
#include <limine/limine_req.h>
#include <debug.h>
#include <string.h>

// Bitmap for tracking page allocation
static uint8_t* bitmap = NULL;
static uint64_t bitmap_size = 0;
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;

// Highest usable physical address
static uint64_t highest_addr = 0;

// HHDM offset for accessing physical memory
extern uint64_t hhdm_offset;
extern struct limine_memmap_response* memmap;

// Helper macros
#define PHYS_TO_VIRT(addr) ((void*)((uint64_t)(addr) + hhdm_offset))
#define VIRT_TO_PHYS(addr) ((uint64_t)(addr) - hhdm_offset)

// Bitmap manipulation
static inline void bitmap_set(uint64_t page) {
    bitmap[page / 8] |= (1 << (page % 8));
}

static inline void bitmap_clear(uint64_t page) {
    bitmap[page / 8] &= ~(1 << (page % 8));
}

static inline bool bitmap_test(uint64_t page) {
    return bitmap[page / 8] & (1 << (page % 8));
}

void pmm_init(void) {
    if (!memmap) {
        log_crit("PMM", "No memory map available");
        return;
    }

    log_info("PMM", "Initializing physical memory manager...");

    // Find the highest usable address
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        uint64_t top = entry->base + entry->length;
        
        if (top > highest_addr) {
            highest_addr = top;
        }
    }

    // Calculate total pages and bitmap size
    total_pages = highest_addr / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8; // Round up to nearest byte

    log_info("PMM", "Total memory: %llu MB", highest_addr / (1024 * 1024));
    log_info("PMM", "Total pages: %llu", total_pages);
    log_info("PMM", "Bitmap size: %llu KB", bitmap_size / 1024);

    // Find a suitable location for the bitmap in usable memory
    bitmap = NULL;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        
        // Look for usable memory that can fit the bitmap
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            bitmap = (uint8_t*)PHYS_TO_VIRT(entry->base);
            log_info("PMM", "Bitmap placed at physical 0x%llx", entry->base);
            break;
        }
    }

    if (!bitmap) {
        log_crit("PMM", "Could not find space for bitmap");
        return;
    }

    // Initialize bitmap - mark all pages as used
    memset(bitmap, 0xFF, bitmap_size);
    used_pages = total_pages;

    // Mark usable memory regions as free
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t base_page = entry->base / PAGE_SIZE;
            uint64_t page_count = entry->length / PAGE_SIZE;
            
            for (uint64_t j = 0; j < page_count; j++) {
                uint64_t page = base_page + j;
                if (page < total_pages) {
                    bitmap_clear(page);
                    used_pages--;
                }
            }
        }
    }

    // Mark the bitmap itself as used
    uint64_t bitmap_phys = VIRT_TO_PHYS((uint64_t)bitmap);
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t bitmap_base_page = bitmap_phys / PAGE_SIZE;
    
    for (uint64_t i = 0; i < bitmap_pages; i++) {
        uint64_t page = bitmap_base_page + i;
        if (page < total_pages && !bitmap_test(page)) {
            bitmap_set(page);
            used_pages++;
        }
    }

    // Mark kernel and module regions as used
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        
        if (entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES ||
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            uint64_t base_page = entry->base / PAGE_SIZE;
            uint64_t page_count = (entry->length + PAGE_SIZE - 1) / PAGE_SIZE;
            
            for (uint64_t j = 0; j < page_count; j++) {
                uint64_t page = base_page + j;
                if (page < total_pages && !bitmap_test(page)) {
                    bitmap_set(page);
                    used_pages++;
                }
            }
        }
    }

    log_ok("PMM", "Initialization complete");
    log_info("PMM", "Free memory: %llu MB", pmm_get_free_memory() / (1024 * 1024));
    log_info("PMM", "Used memory: %llu MB", pmm_get_used_memory() / (1024 * 1024));
}

void* pmm_alloc(void) {
    // Find first free page
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
            
            void* page = (void*)(i * PAGE_SIZE);
            
            // Clear the page
            memset(PHYS_TO_VIRT(page), 0, PAGE_SIZE);
            
            return page;
        }
    }
    
    log_crit("PMM", "Out of physical memory!");
    return NULL;
}

void* pmm_alloc_pages(size_t count) {
    if (count == 0) return NULL;
    if (count == 1) return pmm_alloc();
    
    // Find contiguous free pages
    uint64_t found = 0;
    uint64_t start_page = 0;
    
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            if (found == 0) {
                start_page = i;
            }
            found++;
            
            if (found == count) {
                // Mark all pages as used
                for (uint64_t j = 0; j < count; j++) {
                    bitmap_set(start_page + j);
                    used_pages++;
                }
                
                void* page = (void*)(start_page * PAGE_SIZE);
                
                // Clear all pages
                memset(PHYS_TO_VIRT(page), 0, count * PAGE_SIZE);
                
                return page;
            }
        } else {
            found = 0;
        }
    }
    
    log_crit("PMM", "Out of physical memory! (requested %llu pages)", count);
    return NULL;
}

void pmm_free(void* page) {
    if (!page) return;
    
    uint64_t page_num = (uint64_t)page / PAGE_SIZE;
    
    if (page_num >= total_pages) {
        log_warn("PMM", "Attempt to free invalid page: 0x%llx", (uint64_t)page);
        return;
    }
    
    if (!bitmap_test(page_num)) {
        log_warn("PMM", "Attempt to free already free page: 0x%llx", (uint64_t)page);
        return;
    }
    
    bitmap_clear(page_num);
    used_pages--;
}

void pmm_free_pages(void* page, size_t count) {
    if (!page || count == 0) return;
    
    uint64_t page_num = (uint64_t)page / PAGE_SIZE;
    
    for (size_t i = 0; i < count; i++) {
        uint64_t current_page = page_num + i;
        
        if (current_page >= total_pages) {
            log_warn("PMM", "Attempt to free invalid page: 0x%llx", current_page * PAGE_SIZE);
            continue;
        }
        
        if (!bitmap_test(current_page)) {
            log_warn("PMM", "Attempt to free already free page: 0x%llx", current_page * PAGE_SIZE);
            continue;
        }
        
        bitmap_clear(current_page);
        used_pages--;
    }
}

uint64_t pmm_get_total_memory(void) {
    return total_pages * PAGE_SIZE;
}

uint64_t pmm_get_used_memory(void) {
    return used_pages * PAGE_SIZE;
}

uint64_t pmm_get_free_memory(void) {
    return (total_pages - used_pages) * PAGE_SIZE;
}