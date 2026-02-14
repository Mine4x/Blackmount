#include "heap.h"
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <debug.h>
#include <stdint.h>
#include <string.h>

#define HEAP_MODULE "HEAP"
#define HEAP_VIRTUAL_BASE 0xFFFFFFFF90000000ULL  // Kernel heap base address
#define DEFAULT_HEAP_SIZE (16 * 1024 * 1024)     // 16MB default heap
#define MIN_HEAP_SIZE (4 * 1024 * 1024)          // Minimum 4MB heap
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

static uint64_t align(uint64_t size) {
    return (size + 15) & ~15;\
}

void init_heap(void) {
    if (heap_initialized) {
        log_warn(HEAP_MODULE, "Heap already initialized");
        return;
    }
    
    log_info(HEAP_MODULE, "Initializing kernel heap...");
    
    // Set heap parameters
    heap_start = HEAP_VIRTUAL_BASE;
    heap_size = DEFAULT_HEAP_SIZE;
    
    // Calculate number of pages needed
    uint64_t pages_needed = (heap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    log_info(HEAP_MODULE, "Allocating %llu pages (%llu MB) at virtual address 0x%llx",
             pages_needed, heap_size / (1024 * 1024), heap_start);
    
    // Allocate and map pages for the heap
    void* result = vmm_alloc_pages(vmm_get_kernel_space(), 
                                   (void*)heap_start, 
                                   pages_needed,
                                   PAGE_WRITE | PAGE_PRESENT);
    
    if (!result) {
        log_crit(HEAP_MODULE, "Failed to allocate virtual memory for heap");
        return;
    }
    
    // Initialize the free list with one large free block
    free_list = (Block*)heap_start;
    free_list->magic = BLOCK_MAGIC;
    free_list->size = heap_size - sizeof(Block);
    free_list->is_free = 1;
    free_list->next = NULL;
    
    heap_initialized = 1;
    log_ok(HEAP_MODULE, "Heap initialized: %llu MB available at 0x%llx", 
           heap_size / (1024 * 1024), heap_start);
}

void* kmalloc(uint64_t size) {
    if (!heap_initialized) {
        log_warn(HEAP_MODULE, "Auto-initializing heap on first allocation");
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
    
    uint64_t heap_end = heap_start + heap_size;
    uint64_t ptr_addr = (uint64_t)ptr;
    
    if (ptr_addr < heap_start || ptr_addr >= heap_end) {
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

void defrag_heap(void) {
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