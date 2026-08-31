#ifndef VVECTOR_H
#define VVECTOR_H

#include <stddef.h>

typedef struct {
    void *data;
    size_t size;
    size_t capacity;
    size_t elem_size;
} vvector;

int vvector_init(vvector *v, size_t elem_size);
void vvector_destroy(vvector *v);
int vvector_reserve(vvector *v, size_t capacity);
int vvector_push_back(vvector *v, const void *value);
int vvector_get(const vvector *v, size_t index, void *out_value);
int vvector_pop_back(vvector *v);
int vvector_shrink_to_fit(vvector *v);
int vvector_insert_at(vvector *v, size_t index, const void *value);

#endif