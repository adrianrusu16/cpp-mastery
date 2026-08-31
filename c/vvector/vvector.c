#include "vvector.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    unsigned char stack_buffer[256];
    void *data;
    int heap_allocated;
} vvector_temp_copy;


static vvector_result vvector_make_temp_copy(
    const vvector *v,
    const void *value,
    vvector_temp_copy *temp)
{
    temp->data = temp->stack_buffer;
    temp->heap_allocated = 0;

    if (v->elem_size > sizeof(temp->stack_buffer)) {
        temp->data = malloc(v->elem_size);

        if (temp->data == NULL) {
            return VVECTOR_ERROR_ALLOCATION;
        }

        temp->heap_allocated = 1;
    }

    memcpy(temp->data, value, v->elem_size);

    return VVECTOR_OK;
}


static void vvector_destroy_temp_copy(
    const vvector_temp_copy *temp)
{
    if (temp->heap_allocated) {
        free(temp->data);
    }
}


vvector_result vvector_init(
    vvector *v,
    const size_t elem_size)
{
    if (v == NULL || elem_size == 0) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->elem_size = elem_size;

    return VVECTOR_OK;
}


void vvector_destroy(vvector *v)
{
    if (v == NULL) {
        return;
    }

    free(v->data);

    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}


vvector_result vvector_reserve(
    vvector *v,
    const size_t capacity)
{
    if (v == NULL) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (v->capacity >= capacity) {
        return VVECTOR_OK;
    }

    if (capacity > SIZE_MAX / v->elem_size) {
        return VVECTOR_ERROR_OVERFLOW;
    }

    void *temp =
        realloc(v->data, capacity * v->elem_size);

    if (temp == NULL) {
        return VVECTOR_ERROR_ALLOCATION;
    }

    v->data = temp;
    v->capacity = capacity;

    return VVECTOR_OK;
}


vvector_result vvector_push_back(
    vvector *v,
    const void *value)
{
    if (v == NULL || value == NULL) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    vvector_temp_copy temp;

    vvector_result result =
        vvector_make_temp_copy(v, value, &temp);

    if (result != VVECTOR_OK) {
        return result;
    }

    if (v->size >= v->capacity) {
        size_t new_capacity;

        if (v->capacity == 0) {
            new_capacity = 4;
        } else if (v->capacity > SIZE_MAX / 2) {
            new_capacity = SIZE_MAX / v->elem_size;

            if (new_capacity <= v->capacity) {
                vvector_destroy_temp_copy(&temp);
                return VVECTOR_ERROR_OVERFLOW;
            }
        } else {
            new_capacity = v->capacity * 2;
        }

        result =
            vvector_reserve(v, new_capacity);

        if (result != VVECTOR_OK) {
            vvector_destroy_temp_copy(&temp);
            return result;
        }
    }

    char *target_address =
        (char *)v->data
        + (v->size * v->elem_size);

    memcpy(
        target_address,
        temp.data,
        v->elem_size
    );

    vvector_destroy_temp_copy(&temp);

    v->size++;

    return VVECTOR_OK;
}


vvector_result vvector_get(
    const vvector *v,
    const size_t index,
    void *out_value)
{
    if (v == NULL || out_value == NULL) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (index >= v->size) {
        return VVECTOR_ERROR_OUT_OF_BOUNDS;
    }

    const char *source_address =
        (const char *)v->data
        + (index * v->elem_size);

    memcpy(
        out_value,
        source_address,
        v->elem_size
    );

    return VVECTOR_OK;
}


vvector_result vvector_pop_back(vvector *v)
{
    if (v == NULL) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (v->size == 0) {
        return VVECTOR_ERROR_OUT_OF_BOUNDS;
    }

    v->size--;

    return VVECTOR_OK;
}


vvector_result vvector_shrink_to_fit(vvector *v)
{
    if (v == NULL) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (v->size == v->capacity) {
        return VVECTOR_OK;
    }

    if (v->size == 0) {
        free(v->data);

        v->data = NULL;
        v->capacity = 0;

        return VVECTOR_OK;
    }

    void *temp =
        realloc(
            v->data,
            v->size * v->elem_size
        );

    if (temp == NULL) {
        return VVECTOR_ERROR_ALLOCATION;
    }

    v->data = temp;
    v->capacity = v->size;

    return VVECTOR_OK;
}


vvector_result vvector_insert_at(
    vvector *v,
    const size_t index,
    const void *value)
{
    if (v == NULL || value == NULL) {
        return VVECTOR_ERROR_NULL_ARGUMENT;
    }

    if (index > v->size) {
        return VVECTOR_ERROR_OUT_OF_BOUNDS;
    }

    vvector_temp_copy temp;

    vvector_result result =
        vvector_make_temp_copy(v, value, &temp);

    if (result != VVECTOR_OK) {
        return result;
    }

    if (v->size >= v->capacity) {
        size_t new_capacity;

        if (v->capacity == 0) {
            new_capacity = 4;
        } else if (v->capacity > SIZE_MAX / 2) {
            new_capacity = SIZE_MAX / v->elem_size;

            if (new_capacity <= v->capacity) {
                vvector_destroy_temp_copy(&temp);
                return VVECTOR_ERROR_OVERFLOW;
            }
        } else {
            new_capacity = v->capacity * 2;
        }

        result =
            vvector_reserve(v, new_capacity);

        if (result != VVECTOR_OK) {
            vvector_destroy_temp_copy(&temp);
            return result;
        }
    }

    char *base = v->data;

    char *target_slot = base + (index * v->elem_size);

    const size_t elements_to_move = v->size - index;

    if (elements_to_move > 0) {
        memmove(
            target_slot + v->elem_size,
            target_slot,
            elements_to_move * v->elem_size
        );
    }

    memcpy(
        target_slot,
        temp.data,
        v->elem_size
    );

    vvector_destroy_temp_copy(&temp);

    v->size++;

    return VVECTOR_OK;
}