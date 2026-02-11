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

void init_heap(void);
void* kmalloc(uint64_t size);
void kfree(void* ptr);
void get_heap_stats(HeapStats* stats);
void defrag_heap(void);

#endif // HEAP_H