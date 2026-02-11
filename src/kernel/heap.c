#include "heap.h"
#include "debug.h"
#include <limine/limine_req.h>
#include <stdint.h>

#define HEAP_MODULE "HEAP"
#define MIN_HEAP_SIZE (4 * 1024 * 1024)  // Minimum 4MB heap
#define BLOCK_MAGIC 0xDEADBEEF

typedef struct Block {
    uint32_t magic; 
    uint64_t size; 
    int is_free; 
    struct Block* next; 
} Block;

static Block* free_list = NULL;
static int heap_initialized = 0;
static uint64_t heap_start = 0;
static uint64_t heap_size = 0;

// External references to Limine data
extern uint64_t hhdm_offset;
extern struct limine_memmap_response* memmap;

static uint64_t align(uint64_t size) {
    return (size + 15) & ~15;  // 16-byte alignment for x86_64
}

void init_heap() {
    if (heap_initialized) {
        log_warn(HEAP_MODULE, "Heap already initialized");
        return;
    }
    
    if (!memmap) {
        log_crit(HEAP_MODULE, "Memory map not available");
        return;
    }
    
    log_info(HEAP_MODULE, "Scanning memory map for usable regions...");
    
    // Find the largest usable memory region
    uint64_t best_base = 0;
    uint64_t best_size = 0;
    
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length > best_size) {
            best_base = entry->base;
            best_size = entry->length;
        }
    }
    
    if (best_size < MIN_HEAP_SIZE) {
        log_crit(HEAP_MODULE, "No suitable memory region found (need at least %llu bytes)", MIN_HEAP_SIZE);
        return;
    }
    
    // Use the first portion of the largest region for the heap
    heap_start = best_base;
    heap_size = best_size > (16 * 1024 * 1024) ? (16 * 1024 * 1024) : best_size; // Cap at 16MB
    
    log_info(HEAP_MODULE, "Using memory region: base=0x%llx, size=%llu bytes", heap_start, heap_size);
    
    // Convert physical address to virtual using HHDM
    free_list = (Block*)(heap_start + hhdm_offset);
    free_list->magic = BLOCK_MAGIC;
    free_list->size = heap_size - sizeof(Block);
    free_list->is_free = 1;
    free_list->next = NULL;
    
    heap_initialized = 1;
    log_ok(HEAP_MODULE, "Heap initialized: %llu MB available", heap_size / (1024 * 1024));
}

void* kmalloc(uint64_t size) {
    if (!heap_initialized) {
        init_heap();
    }
    
    if (!heap_initialized) {
        log_crit(HEAP_MODULE, "Heap initialization failed");
        return NULL;
    }
    
    if (size == 0) {
        log_warn(HEAP_MODULE, "Attempted to allocate 0 bytes");
        return NULL;
    }
    
    uint64_t aligned_size = align(size);
    
    Block* current = free_list;
    
    while (current) {
        if (current->magic != BLOCK_MAGIC) {
            log_crit(HEAP_MODULE, "Heap corruption detected at block %p", current);
            return NULL;
        }
        
        if (current->is_free && current->size >= aligned_size) {
            // Split block if there's enough space for another block
            if (current->size >= aligned_size + sizeof(Block) + 16) {
                Block* new_block = (Block*)((char*)current + sizeof(Block) + aligned_size);
                new_block->magic = BLOCK_MAGIC;
                new_block->size = current->size - aligned_size - sizeof(Block);
                new_block->is_free = 1;
                new_block->next = current->next;
                
                current->size = aligned_size;
                current->next = new_block;
            }
            
            current->is_free = 0;
            void* ptr = (void*)((char*)current + sizeof(Block));
            return ptr;
        }
        
        current = current->next;
    }
    
    log_err(HEAP_MODULE, "Out of memory: failed to allocate %llu bytes", size);
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) {
        log_warn(HEAP_MODULE, "Attempted to free NULL pointer");
        return;
    }
    
    if (!heap_initialized) {
        log_err(HEAP_MODULE, "Attempted to free before heap initialization");
        return;
    }
    
    uint64_t heap_end = heap_start + hhdm_offset + heap_size;
    uint64_t ptr_addr = (uint64_t)ptr;
    
    if (ptr_addr < (heap_start + hhdm_offset) || ptr_addr >= heap_end) {
        log_err(HEAP_MODULE, "Attempted to free pointer outside heap: %p", ptr);
        return;
    }
    
    Block* block = (Block*)((char*)ptr - sizeof(Block));
    
    if (block->magic != BLOCK_MAGIC) {
        log_err(HEAP_MODULE, "Invalid block magic at %p (corruption or invalid pointer)", ptr);
        return;
    }
    
    if (block->is_free) {
        log_warn(HEAP_MODULE, "Double free detected at %p", ptr);
        return;
    }
    
    block->is_free = 1;
    
    // Coalesce with next block if it's free
    if (block->next && block->next->is_free) {
        block->size += sizeof(Block) + block->next->size;
        block->next = block->next->next;
    }
    
    // Coalesce with previous block if it's free
    Block* current = free_list;
    while (current && current->next != block) {
        current = current->next;
    }
    
    if (current && current->is_free) {
        current->size += sizeof(Block) + block->size;
        current->next = block->next;
    }
}

void get_heap_stats(HeapStats* stats) {
    if (!heap_initialized) {
        log_warn(HEAP_MODULE, "get_heap_stats called before heap initialization");
        return;
    }
    
    if (!stats) {
        log_err(HEAP_MODULE, "get_heap_stats called with NULL stats pointer");
        return;
    }
    
    stats->total_size = heap_size;
    stats->used_size = 0;
    stats->free_size = 0;
    stats->num_blocks = 0;
    stats->num_free_blocks = 0;
    
    Block* current = free_list;
    while (current) {
        stats->num_blocks++;
        if (current->is_free) {
            stats->num_free_blocks++;
            stats->free_size += current->size;
        } else {
            stats->used_size += current->size;
        }
        current = current->next;
    }
}

void defrag_heap() {
    if (!heap_initialized) {
        log_err(HEAP_MODULE, "defrag_heap called before heap initialization");
        return;
    }
    
    int merged_count = 0;
    Block* current = free_list;
    while (current) {
        if (current->is_free && current->next && current->next->is_free) {
            current->size += sizeof(Block) + current->next->size;
            current->next = current->next->next;
            merged_count++;
        } else {
            current = current->next;
        }
    }
    
    if (merged_count > 0) {
        log_info(HEAP_MODULE, "Defragmentation merged %d blocks", merged_count);
    }
}