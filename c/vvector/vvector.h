#ifndef VVECTOR_H
#define VVECTOR_H

#include <stddef.h>

typedef struct {
    void *data;
    size_t size;
    size_t capacity;
    size_t elem_size;
} vvector;

typedef enum {
    VVECTOR_OK = 0,
    VVECTOR_ERROR_NULL_ARGUMENT,
    VVECTOR_ERROR_OUT_OF_BOUNDS,
    VVECTOR_ERROR_ALLOCATION,
    VVECTOR_ERROR_OVERFLOW
} vvector_result;

vvector_result vvector_init(vvector *v, size_t elem_size);
void vvector_destroy(vvector *v);
vvector_result vvector_reserve(vvector *v, size_t capacity);
vvector_result vvector_push_back(vvector *v, const void *value);
vvector_result vvector_get(const vvector *v, size_t index, void *out_value);
vvector_result vvector_pop_back(vvector *v);
vvector_result vvector_shrink_to_fit(vvector *v);
vvector_result vvector_insert_at(
    vvector *v,
    size_t index,
    const void *value
);

#endif