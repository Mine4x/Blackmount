#pragma once
#include <stddef.h>
#include <heap.h>
#include <memory.h>

typedef struct {
    void *data;
    size_t size;
    size_t capacity;
    size_t elem_size;
} vector;

void vector_init(vector *v, size_t elem_size)
{
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->elem_size = elem_size;
}

void vector_reserve(vector *v, size_t capacity)
{
    if (capacity <= v->capacity) return;

    void *new_data = kmalloc(capacity * v->elem_size);

    if (v->data)
        memmove(new_data, v->data, v->size * v->elem_size);

    kfree(v->data);

    v->data = new_data;
    v->capacity = capacity;
}

void vector_push(vector *v, void *elem)
{
    if (v->size >= v->capacity) {
        size_t new_cap = v->capacity ? v->capacity * 2 : 4;
        vector_reserve(v, new_cap);
    }

    void *dest = (char*)v->data + (v->size * v->elem_size);
    memmove(dest, elem, v->elem_size);
    v->size++;
}

void *vector_get(vector *v, size_t index)
{
    return (char*)v->data + (index * v->elem_size);
}

void vector_pop(vector *v)
{
    if (v->size)
        v->size--;
}

void vector_clear(vector *v)
{
    v->size = 0;
}

void vector_free(vector *v)
{
    if (v->data)
        kfree(v->data);

    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}