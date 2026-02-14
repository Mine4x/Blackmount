#ifndef HEAP_H
#define HEAP_H

void init_heap();

typedef struct {
    unsigned int total_size;
    unsigned int used_size;
    unsigned int free_size;
    unsigned int num_blocks;
    unsigned int num_free_blocks;
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

void defrag_heap();

#endif