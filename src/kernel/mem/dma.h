#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define DMA_ZONE_NORMAL  0
#define DMA_ZONE_ISA     1

void dma_init(void);

void *dma_alloc(size_t size,
                size_t alignment,
                size_t boundary,
                int zone);

void dma_free(void *ptr);

void dma_cache_flush(void *ptr, size_t size);
void dma_cache_invalidate(void *ptr, size_t size);

#endif