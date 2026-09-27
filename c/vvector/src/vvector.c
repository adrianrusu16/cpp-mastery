#include "vvector.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { VVECTOR_INLINE_COPY_BYTES = 256 };

typedef struct {
    unsigned char inline_buffer[VVECTOR_INLINE_COPY_BYTES];
    void *data;
    int heap_allocated;
} vvector_temp_copy;

static vvector_result vvector_make_temp_copy(
    const vvector *vector,
    const void *value,
    vvector_temp_copy *temp)
{
    temp->data = temp->inline_buffer;
    temp->heap_allocated = 0;

    if (vector->elem_size > sizeof(temp->inline_buffer)) {
        temp->data = malloc(vector->elem_size);
        if (temp->data == NULL) {
            return VVECTOR_ERROR_ALLOCATION;
        }
        temp->heap_allocated = 1;
    }

    memcpy(temp->data, value, vector->elem_size);
    return VVECTOR_OK;
}

static void vvector_destroy_temp_copy(const vvector_temp_copy *temp)
{
    if (temp->heap_allocated != 0) {
        free(temp->data);
    }
}

static vvector_result vvector_next_capacity(
    const vvector *vector,
    size_t *new_capacity)
{
    if (vector->elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    const size_t max_capacity = SIZE_MAX / vector->elem_size;
    if (vector->capacity >= max_capacity) {
        return VVECTOR_ERROR_OVERFLOW;
    }

    if (vector->capacity == 0) {
        *new_capacity = max_capacity < 4 ? max_capacity : 4;
        return *new_capacity == 0 ? VVECTOR_ERROR_OVERFLOW : VVECTOR_OK;
    }

    *new_capacity = vector->capacity > max_capacity / 2
        ? max_capacity
        : vector->capacity * 2;

    return *new_capacity > vector->capacity
        ? VVECTOR_OK
        : VVECTOR_ERROR_OVERFLOW;
}

static vvector_result vvector_ensure_one_more(vvector *vector)
{
    if (vector->size < vector->capacity) {
        return VVECTOR_OK;
    }

    size_t new_capacity = 0;
    const vvector_result result = vvector_next_capacity(vector, &new_capacity);
    if (result != VVECTOR_OK) {
        return result;
    }

    return vvector_reserve(vector, new_capacity);
}

vvector_result vvector_init(vvector *vector, const size_t elem_size)
{
    if (vector == NULL || elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    vector->data = NULL;
    vector->size = 0;
    vector->capacity = 0;
    vector->elem_size = elem_size;
    return VVECTOR_OK;
}

void vvector_destroy(vvector *vector)
{
    if (vector == NULL) {
        return;
    }

    free(vector->data);
    vector->data = NULL;
    vector->size = 0;
    vector->capacity = 0;
    vector->elem_size = 0;
}

vvector_result vvector_reserve(vvector *vector, const size_t capacity)
{
    if (vector == NULL || vector->elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (capacity <= vector->capacity) {
        return VVECTOR_OK;
    }

    if (capacity > SIZE_MAX / vector->elem_size) {
        return VVECTOR_ERROR_OVERFLOW;
    }

    void *new_data = realloc(vector->data, capacity * vector->elem_size);
    if (new_data == NULL) {
        return VVECTOR_ERROR_ALLOCATION;
    }

    vector->data = new_data;
    vector->capacity = capacity;
    return VVECTOR_OK;
}

vvector_result vvector_push_back(vvector *vector, const void *value)
{
    if (vector == NULL || value == NULL || vector->elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    vvector_temp_copy temp;
    vvector_result result = vvector_make_temp_copy(vector, value, &temp);
    if (result != VVECTOR_OK) {
        return result;
    }

    result = vvector_ensure_one_more(vector);
    if (result != VVECTOR_OK) {
        vvector_destroy_temp_copy(&temp);
        return result;
    }

    unsigned char *target = (unsigned char *)vector->data
        + (vector->size * vector->elem_size);
    memcpy(target, temp.data, vector->elem_size);

    vvector_destroy_temp_copy(&temp);
    ++vector->size;
    return VVECTOR_OK;
}

vvector_result vvector_get(
    const vvector *vector,
    const size_t index,
    void *out_value)
{
    if (vector == NULL || out_value == NULL || vector->elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (index >= vector->size) {
        return VVECTOR_ERROR_OUT_OF_BOUNDS;
    }

    const unsigned char *source = (const unsigned char *)vector->data
        + (index * vector->elem_size);
    memcpy(out_value, source, vector->elem_size);
    return VVECTOR_OK;
}

vvector_result vvector_pop_back(vvector *vector)
{
    if (vector == NULL || vector->elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (vector->size == 0) {
        return VVECTOR_ERROR_OUT_OF_BOUNDS;
    }

    --vector->size;
    return VVECTOR_OK;
}

vvector_result vvector_shrink_to_fit(vvector *vector)
{
    if (vector == NULL || vector->elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (vector->size == vector->capacity) {
        return VVECTOR_OK;
    }

    if (vector->size == 0) {
        free(vector->data);
        vector->data = NULL;
        vector->capacity = 0;
        return VVECTOR_OK;
    }

    void *new_data = realloc(
        vector->data,
        vector->size * vector->elem_size);
    if (new_data == NULL) {
        return VVECTOR_ERROR_ALLOCATION;
    }

    vector->data = new_data;
    vector->capacity = vector->size;
    return VVECTOR_OK;
}

vvector_result vvector_insert_at(
    vvector *vector,
    const size_t index,
    const void *value)
{
    if (vector == NULL || value == NULL || vector->elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (index > vector->size) {
        return VVECTOR_ERROR_OUT_OF_BOUNDS;
    }

    vvector_temp_copy temp;
    vvector_result result = vvector_make_temp_copy(vector, value, &temp);
    if (result != VVECTOR_OK) {
        return result;
    }

    result = vvector_ensure_one_more(vector);
    if (result != VVECTOR_OK) {
        vvector_destroy_temp_copy(&temp);
        return result;
    }

    unsigned char *base = (unsigned char *)vector->data;
    unsigned char *target = base + (index * vector->elem_size);
    const size_t elements_to_move = vector->size - index;

    if (elements_to_move != 0) {
        memmove(
            target + vector->elem_size,
            target,
            elements_to_move * vector->elem_size);
    }

    memcpy(target, temp.data, vector->elem_size);
    vvector_destroy_temp_copy(&temp);

    ++vector->size;
    return VVECTOR_OK;
}
