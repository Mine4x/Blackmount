#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>

typedef struct {
    uint64_t total_size;
    uint64_t used_size;
    uint64_t free_size;
    uint64_t num_blocks;
    uint64_t num_free_blocks;
} HeapStats;

// Initialize the kernel heap
void init_heap(void);

// Allocate memory from heap
void* kmalloc(uint64_t size);

// Free memory back to heap
void kfree(void* ptr);

// Get heap statistics
void get_heap_stats(HeapStats* stats);

// Defragment the heap by merging adjacent free blocks
void defrag_heap(void);

#endif // HEAP_H