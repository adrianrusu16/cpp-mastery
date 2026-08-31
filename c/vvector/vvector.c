#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void *data;
    size_t size;
    size_t capacity;
    size_t elem_size;
} vvector;

typedef struct {
    unsigned char stack_buffer[256];
    void *data;
    int heap_allocated;
} vvector_temp_copy;

static int vvector_make_temp_copy(
    const vvector *v,
    const void *value,
    vvector_temp_copy *temp)
{
    temp->data = temp->stack_buffer;
    temp->heap_allocated = 0;

    if (v->elem_size > sizeof(temp->stack_buffer)) {
        temp->data = malloc(v->elem_size);

        if (temp->data == NULL)
            return 1;

        temp->heap_allocated = 1;
    }

    memcpy(temp->data, value, v->elem_size);

    return 0;
}

static void vvector_destroy_temp_copy(const vvector_temp_copy *temp)
{
    if (temp->heap_allocated)
        free(temp->data);
}

int vvector_init(vvector *v, size_t elem_size)
{
    if (v == NULL || elem_size == 0)
        return -1;

    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->elem_size = elem_size;

    return 0;
}

void vvector_destroy(vvector *v)
{
    if (v == NULL)
        return;

    free(v->data);

    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

int vvector_reserve(vvector *v, size_t capacity)
{
    if (v == NULL)
        return 1;

    if (v->capacity >= capacity)
        return 0;

    if (capacity > SIZE_MAX / v->elem_size)
        return 2;

    void *temp = realloc(v->data, capacity * v->elem_size);

    if (temp == NULL)
        return 3;

    v->data = temp;
    v->capacity = capacity;

    return 0;
}

int vvector_push_back(vvector *v, const void *value)
{
    if (v == NULL || value == NULL) {
        return 1;
    }

    vvector_temp_copy temp;

    if (vvector_make_temp_copy(v, value, &temp) != 0) {
        return 2;
    }

    if (v->size >= v->capacity) {
        size_t new_capacity;

        if (v->capacity == 0) {
            new_capacity = 4;
        } else if (v->capacity > SIZE_MAX / 2) {
            new_capacity = SIZE_MAX / v->elem_size;

            if (new_capacity <= v->capacity) {
                vvector_destroy_temp_copy(&temp);
                return 3;
            }
        } else {
            new_capacity = v->capacity * 2;
        }

        if (vvector_reserve(v, new_capacity) != 0) {
            vvector_destroy_temp_copy(&temp);
            return 4;
        }
    }

    char *target_address =
        (char *)v->data + (v->size * v->elem_size);

    memcpy(target_address, temp.data, v->elem_size);

    vvector_destroy_temp_copy(&temp);

    v->size++;

    return 0;
}

int vvector_get(const vvector *v, size_t index, void *out_value)
{
    if (v == NULL || out_value == NULL) {
        return 1;
    }

    if (index >= v->size) {
        return 2;
    }

    const char *source_address = (const char *)v->data + (index * v->elem_size);

    memcpy(out_value, source_address, v->elem_size);

    return 0;
}

int vvector_pop_back(vvector *v)
{
    if (v == NULL) {
        return 1;
    }

    if (v->size == 0) {
        return 2;
    }

    v->size--;

    return 0;
}

int vvector_shrink_to_fit(vvector *v)
{
    if (v == NULL) {
        return 1;
    }

    if (v->size == v->capacity) {
        return 0;
    }

    if (v->size == 0) {
        free(v->data);
        v->data = NULL;
        v->capacity = 0;
        return 0;
    }

    void *temp = realloc(v->data, v->size * v->elem_size);
    if (temp == NULL) {
        return 2;
    }

    v->data = temp;
    v->capacity = v->size;

    return 0;
}

int vvector_insert_at(vvector *v, size_t index, const void *value)
{
    if (v == NULL || value == NULL)
        return 1;

    if (index > v->size)
        return 2;

    vvector_temp_copy temp;

    if (vvector_make_temp_copy(v, value, &temp) != 0)
        return 3;

    if (v->size >= v->capacity) {
        size_t new_capacity;

        if (v->capacity == 0) {
            new_capacity = 4;
        } else if (v->capacity > SIZE_MAX / 2) {
            new_capacity = SIZE_MAX / v->elem_size;

            if (new_capacity <= v->capacity) {
                vvector_destroy_temp_copy(&temp);
                return 4;
            }
        } else {
            new_capacity = v->capacity * 2;
        }

        if (vvector_reserve(v, new_capacity) != 0) {
            vvector_destroy_temp_copy(&temp);
            return 5;
        }
    }

    char *base = v->data;
    char *target_slot = base + index * v->elem_size;

    size_t elements_to_move = v->size - index;

    if (elements_to_move > 0) {
        memmove(
            target_slot + v->elem_size,
            target_slot,
            elements_to_move * v->elem_size
        );
    }

    memcpy(target_slot, temp.data, v->elem_size);

    vvector_destroy_temp_copy(&temp);

    v->size++;

    return 0;
}