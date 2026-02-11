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

void get_heap_stats(HeapStats* stats);

void defrag_heap();

#endif