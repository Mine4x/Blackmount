#include "heap.h"
#include "debug.h"

#define HEAP_MODULE "HEAP"
#define HEAP_SIZE (1024 * 1024)  // 1MB heap
#define BLOCK_MAGIC 0xDEADBEEF

typedef struct Block {
    unsigned int magic; 
    unsigned int size; 
    int is_free; 
    struct Block* next; 
} Block;

static char heap[HEAP_SIZE];
static Block* free_list = 0;
static int heap_initialized = 0;

static unsigned int align(unsigned int size) {
    return (size + 7) & ~7;
}

void init_heap() {
    if (heap_initialized) {
        log_warn(HEAP_MODULE, "Heap already initialized");
        return;
    }
    
    log_info(HEAP_MODULE, "Initializing heap (%d bytes)", HEAP_SIZE);
    
    free_list = (Block*)heap;
    free_list->magic = BLOCK_MAGIC;
    free_list->size = HEAP_SIZE - sizeof(Block);
    free_list->is_free = 1;
    free_list->next = 0;
    
    heap_initialized = 1;
    log_ok(HEAP_MODULE, "Heap initialized successfully");
}

void* kmalloc(unsigned int size) {
    if (!heap_initialized) {
        init_heap();
    }
    
    if (size == 0) {
        log_warn(HEAP_MODULE, "Attempted to allocate 0 bytes");
        return 0;
    }
    
    unsigned int aligned_size = align(size);
    
    Block* current = free_list;
    
    while (current) {
        if (current->magic != BLOCK_MAGIC) {
            log_crit(HEAP_MODULE, "Heap corruption detected at block %p", current);
            return 0;
        }
        
        if (current->is_free && current->size >= aligned_size) {
            if (current->size >= aligned_size + sizeof(Block) + 8) {
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
    
    log_err(HEAP_MODULE, "Out of memory: failed to allocate %d bytes", size);
    return 0;
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
    
    char* p = (char*)ptr;
    if (p < heap || p >= heap + HEAP_SIZE) {
        log_err(HEAP_MODULE, "Attempted to free pointer outside heap: %p", ptr);
        return;
    }
    
    Block* block = (Block*)(p - sizeof(Block));
    
    if (block->magic != BLOCK_MAGIC) {
        log_err(HEAP_MODULE, "Invalid block magic at %p (corruption or invalid pointer)", ptr);
        return;
    }
    
    if (block->is_free) {
        log_warn(HEAP_MODULE, "Double free detected at %p", ptr);
        return;
    }
    
    block->is_free = 1;
    
    if (block->next && block->next->is_free) {
        block->size += sizeof(Block) + block->next->size;
        block->next = block->next->next;
    }
    
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
    
    stats->total_size = HEAP_SIZE;
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
}