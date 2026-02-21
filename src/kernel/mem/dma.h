#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


#define DMA_ZONE_NORMAL  0
#define DMA_ZONE_ISA     1

typedef struct {
    void     *virt;    // Kernel virtual address — use this in your driver 
    uint64_t  phys;    // Physical (bus) address — hand this to the device
    size_t    size;    // Requested size in bytes
    size_t    pages;   // Number of 4 KiB pages backing this buffer
    int       zone;    // DMA_ZONE_* used at allocation time
    bool      bounce;  // true for ISA zone buffers (use sync helpers)
} dma_buf_t;


void dma_init(void);
dma_buf_t *dma_alloc(size_t size, int zone);
void dma_free(dma_buf_t *buf);
void dma_bounce_sync_to_device(dma_buf_t *buf, const void *src, size_t len);
void dma_bounce_sync_from_device(dma_buf_t *buf, void *dst, size_t len);
void dma_cache_flush(dma_buf_t *buf);
void dma_cache_invalidate(dma_buf_t *buf);

#endif // DMA_H