#include <stdlib.h>
#include <syscalls.h>
#include <stdint.h>
#include <stddef.h>


#define ALIGN       16

typedef struct block {
    size_t        size;
    int           free;
    struct block *next;
} block_t;

#define BLOCK_HDR sizeof(block_t)

static block_t *heap_head = NULL;

static void *sbrk(size_t increment)
{
    uint64_t cur = brk(0);
    if (cur == (uint64_t)-1)
        return (void *)-1;
    uint64_t next = cur + increment;
    if (brk(next) == (uint64_t)-1)
        return (void *)-1;
    return (void *)cur;
}

static block_t *find_free(size_t size)
{
    block_t *b = heap_head;
    while (b) {
        if (b->free && b->size >= size)
            return b;
        b = b->next;
    }
    return NULL;
}

static block_t *extend(size_t size)
{
    size_t   total = BLOCK_HDR + size;
    block_t *b     = sbrk(total);
    if (b == (void *)-1)
        return NULL;
    b->size = size;
    b->free = 0;
    b->next = NULL;
    return b;
}

static void split(block_t *b, size_t size)
{
    if (b->size < size + BLOCK_HDR + ALIGN)
        return;

    block_t *split_block = (block_t *)((uint8_t *)b + BLOCK_HDR + size);
    split_block->size    = b->size - size - BLOCK_HDR;
    split_block->free    = 1;
    split_block->next    = b->next;

    b->size = size;
    b->next = split_block;
}

static void coalesce(void)
{
    block_t *b = heap_head;
    while (b && b->next) {
        if (b->free && b->next->free) {
            b->size += BLOCK_HDR + b->next->size;
            b->next  = b->next->next;
        } else {
            b = b->next;
        }
    }
}

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size = (size + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    block_t *b = find_free(size);

    if (b) {
        split(b, size);
        b->free = 0;
        return (uint8_t *)b + BLOCK_HDR;
    }

    b = extend(size);
    if (!b)
        return NULL;

    if (!heap_head) {
        heap_head = b;
    } else {
        block_t *cur = heap_head;
        while (cur->next)
            cur = cur->next;
        cur->next = b;
    }

    return (uint8_t *)b + BLOCK_HDR;
}

void free(void *ptr)
{
    if (!ptr)
        return;

    block_t *b = (block_t *)((uint8_t *)ptr - BLOCK_HDR);
    b->free = 1;

    coalesce();
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_t *b        = (block_t *)((uint8_t *)ptr - BLOCK_HDR);
    size_t   aligned  = (size + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    if (b->size >= aligned)
        return ptr;

    void *new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    size_t copy = b->size < aligned ? b->size : aligned;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < copy; i++)
        dst[i] = src[i];

    free(ptr);
    return new_ptr;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size)
        return NULL;

    void    *ptr = malloc(total);
    uint8_t *p   = (uint8_t *)ptr;
    if (ptr)
        for (size_t i = 0; i < total; i++)
            p[i] = 0;

    return ptr;
}