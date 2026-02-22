#include "dma.h"
#include "pmm.h"
#include "vmm.h"
#include <limine/limine_req.h>
#include <memory.h>
#include <util/spinlock.h>

#define DMA_VIRT_BASE        0xFFFFE00000000000ULL
#define DMA_VIRT_PAGE_COUNT  65536UL

#define ISA_LIMIT_PHYS       (16ULL * 1024 * 1024)

#define ISA_POOL_PAGES       64
#define META_POOL_CAP        256
#define ISA_POOL_MAX_TRIES   (ISA_POOL_PAGES * 8)

#define SPINLOCK_INIT { 0 }

static uint64_t   virt_bitmap[DMA_VIRT_PAGE_COUNT / 64];
static spinlock_t virt_lock = SPINLOCK_INIT;

static void *virt_range_alloc(size_t pages) {
    spin_lock(&virt_lock);

    size_t run = 0, start = 0;
    for (size_t i = 0; i < DMA_VIRT_PAGE_COUNT; i++) {
        bool free = !(virt_bitmap[i >> 6] & (1ULL << (i & 63)));
        if (free) {
            if (!run) start = i;
            if (++run == pages) {
                for (size_t j = start; j <= i; j++)
                    virt_bitmap[j >> 6] |= 1ULL << (j & 63);
                spin_unlock(&virt_lock);
                return (void *)(DMA_VIRT_BASE + start * PAGE_SIZE);
            }
        } else {
            run = 0;
        }
    }

    spin_unlock(&virt_lock);
    return NULL;
}

static void virt_range_free(void *virt, size_t pages) {
    size_t start = ((uintptr_t)virt - DMA_VIRT_BASE) / PAGE_SIZE;
    spin_lock(&virt_lock);
    for (size_t i = start; i < start + pages; i++)
        virt_bitmap[i >> 6] &= ~(1ULL << (i & 63));
    spin_unlock(&virt_lock);
}

typedef struct isa_node { uint64_t phys; struct isa_node *next; } isa_node_t;

static isa_node_t  isa_nodes[ISA_POOL_PAGES];
static isa_node_t *isa_free_list = NULL;
static int         isa_pool_size = 0;
static spinlock_t  isa_lock = SPINLOCK_INIT;

static void isa_pool_init(void) {
    for (int tries = 0;
         tries < ISA_POOL_MAX_TRIES && isa_pool_size < ISA_POOL_PAGES;
         tries++)
    {
        void *p = pmm_alloc();
        if (!p) break;

        if ((uint64_t)p >= ISA_LIMIT_PHYS) {
            pmm_free(p);
            continue;
        }

        isa_nodes[isa_pool_size].phys = (uint64_t)p;
        isa_nodes[isa_pool_size].next = isa_free_list;
        isa_free_list = &isa_nodes[isa_pool_size];
        isa_pool_size++;
    }
}

static uint64_t isa_page_alloc(void) {
    spin_lock(&isa_lock);
    if (!isa_free_list) {
        spin_unlock(&isa_lock);
        return 0;
    }
    isa_node_t *n = isa_free_list;
    isa_free_list = n->next;
    spin_unlock(&isa_lock);
    return n->phys;
}

static void isa_page_free(uint64_t phys) {
    for (int i = 0; i < isa_pool_size; i++) {
        if (isa_nodes[i].phys == phys) {
            spin_lock(&isa_lock);
            isa_nodes[i].next = isa_free_list;
            isa_free_list = &isa_nodes[i];
            spin_unlock(&isa_lock);
            return;
        }
    }
    pmm_free((void *)phys);
}

static dma_buf_t  meta_pool[META_POOL_CAP];
static bool       meta_used[META_POOL_CAP];
static spinlock_t meta_lock = SPINLOCK_INIT;

static dma_buf_t *meta_alloc(void) {
    spin_lock(&meta_lock);
    for (int i = 0; i < META_POOL_CAP; i++) {
        if (!meta_used[i]) {
            meta_used[i] = true;
            spin_unlock(&meta_lock);
            return &meta_pool[i];
        }
    }
    spin_unlock(&meta_lock);
    return NULL;
}

static void meta_free(dma_buf_t *buf) {
    int idx = (int)(buf - meta_pool);
    spin_lock(&meta_lock);
    meta_used[idx] = false;
    spin_unlock(&meta_lock);
}

#define DMA_MAP_FLAGS  (PAGE_PRESENT | PAGE_WRITE | PAGE_NOCACHE | PAGE_GLOBAL)

void dma_init(void) {
    isa_pool_init();
}

dma_buf_t *dma_alloc(size_t size, int zone) {
    if (!size) return NULL;

    size_t   pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t phys  = 0;
    bool     from_isa_pool = false;

    if (zone == DMA_ZONE_ISA) {
        if (pages == 1) {
            phys = isa_page_alloc();
            if (!phys) return NULL;
            from_isa_pool = true;
        } else {
            void *p = pmm_alloc_pages(pages);
            if (!p) return NULL;
            if ((uint64_t)p >= ISA_LIMIT_PHYS) {
                pmm_free_pages(p, pages);
                return NULL;
            }
            phys = (uint64_t)p;
        }
    } else {
        void *p = pmm_alloc_pages(pages);
        if (!p) return NULL;
        phys = (uint64_t)p;
    }

    void *virt = virt_range_alloc(pages);
    if (!virt) goto err_free_phys;

    address_space_t *ks = vmm_get_kernel_space();
    if (!vmm_map_range(ks, virt, (void *)phys, pages, DMA_MAP_FLAGS))
        goto err_free_virt;

    memset(virt, 0, pages * PAGE_SIZE);

    dma_buf_t *buf = meta_alloc();
    if (!buf) goto err_unmap;

    buf->virt   = virt;
    buf->phys   = phys;
    buf->size   = size;
    buf->pages  = pages;
    buf->zone   = zone;
    buf->bounce = (zone == DMA_ZONE_ISA);

    return buf;

err_unmap:
    vmm_unmap_range(ks, virt, pages);
err_free_virt:
    virt_range_free(virt, pages);
err_free_phys:
    if (from_isa_pool)
        isa_page_free(phys);
    else
        pmm_free_pages((void *)phys, pages);
    return NULL;
}

void dma_free(dma_buf_t *buf) {
    if (!buf) return;

    address_space_t *ks = vmm_get_kernel_space();

    vmm_unmap_range(ks, buf->virt, buf->pages);
    virt_range_free(buf->virt, buf->pages);

    if (buf->zone == DMA_ZONE_ISA && buf->pages == 1)
        isa_page_free(buf->phys);
    else
        pmm_free_pages((void *)buf->phys, buf->pages);

    meta_free(buf);
}

void dma_bounce_sync_to_device(dma_buf_t *buf, const void *src, size_t len) {
    if (!buf || !buf->bounce || !src) return;
    if (len > buf->size) len = buf->size;
    memcpy(buf->virt, src, len);
    dma_cache_flush(buf);
}

void dma_bounce_sync_from_device(dma_buf_t *buf, void *dst, size_t len) {
    if (!buf || !buf->bounce || !dst) return;
    if (len > buf->size) len = buf->size;
    dma_cache_invalidate(buf);
    memcpy(dst, buf->virt, len);
}

void dma_cache_flush(dma_buf_t *buf) {
    if (!buf) return;
    uintptr_t addr = (uintptr_t)buf->virt;
    uintptr_t end  = addr + buf->size;
    addr &= ~(uintptr_t)63;
    for (; addr < end; addr += 64)
        __asm__ volatile("clflush (%0)" :: "r"(addr) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}

void dma_cache_invalidate(dma_buf_t *buf) {
    dma_cache_flush(buf);
}