#ifndef CPP_MASTERY_VVECTOR_TEST_SUPPORT_H
#define CPP_MASTERY_VVECTOR_TEST_SUPPORT_H

#include <stddef.h>

#include "vvector.h"

typedef struct {
    unsigned char payload[512];
    int id;
} LargeObject;

static inline int expect_int_vector(
    const char *test_name,
    const vvector *vector,
    const int *expected,
    const size_t expected_size)
{
    (void)test_name;

    if (vector->size != expected_size) {
        return 0;
    }

    const int *data = (const int *)vector->data;
    for (size_t i = 0; i < expected_size; ++i) {
        if (data[i] != expected[i]) {
            return 0;
        }
    }

    return 1;
}

#endif
