#include "test_support.h"
#include "test_cases.h"

int test_push_normal(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    for (int i = 0; i < 10; ++i) {
        if (vvector_push_back(&v, &i) != VVECTOR_OK) {
            vvector_destroy(&v);
            return 0;
        }
    }

    const int expected[] = {
        0, 1, 2, 3, 4,
        5, 6, 7, 8, 9
    };

    const int ok =
        expect_int_vector(
            "push normal",
            &v,
            expected,
            sizeof(expected) / sizeof(expected[0])
        )
        && v.capacity == 16;

    vvector_destroy(&v);

    return ok;
}

int test_get(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    for (int i = 0; i < 10; ++i) {
        if (vvector_push_back(&v, &i) != VVECTOR_OK) {
            vvector_destroy(&v);
            return 0;
        }
    }

    int value = -1;

    vvector_result result =
        vvector_get(&v, 5, &value);

    if (result != VVECTOR_OK || value != 5) {
        vvector_destroy(&v);
        return 0;
    }

    result =
        vvector_get(&v, 99, &value);

    if (result != VVECTOR_ERROR_OUT_OF_BOUNDS) {
        vvector_destroy(&v);
        return 0;
    }

    vvector_destroy(&v);

    return 1;
}

int test_pop_back(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    for (int i = 0; i < 3; ++i) {
        if (vvector_push_back(&v, &i) != VVECTOR_OK) {
            vvector_destroy(&v);
            return 0;
        }
    }

    const vvector_result result =
        vvector_pop_back(&v);

    if (result != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        0, 1
    };

    const int ok =
        expect_int_vector(
            "pop_back",
            &v,
            expected,
            sizeof(expected) / sizeof(expected[0])
        );

    vvector_destroy(&v);

    return ok;
}

int test_pop_back_empty(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    const vvector_result result =
        vvector_pop_back(&v);

    vvector_destroy(&v);

    return result == VVECTOR_ERROR_OUT_OF_BOUNDS;
}

int test_shrink_to_fit(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    for (int i = 0; i < 10; ++i) {
        if (vvector_push_back(&v, &i) != VVECTOR_OK) {
            vvector_destroy(&v);
            return 0;
        }
    }

    if (vvector_pop_back(&v) != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    if (vvector_shrink_to_fit(&v) != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        0, 1, 2, 3, 4,
        5, 6, 7, 8
    };

    const int ok =
        expect_int_vector(
            "shrink_to_fit",
            &v,
            expected,
            sizeof(expected) / sizeof(expected[0])
        )
        && v.capacity == v.size
        && v.capacity == 9;

    vvector_destroy(&v);

    return ok;
}

int test_insert(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    for (int i = 0; i < 3; ++i) {
        if (vvector_push_back(&v, &i) != VVECTOR_OK) {
            vvector_destroy(&v);
            return 0;
        }
    }

    int value = 99;

    if (vvector_insert_at(&v, 0, &value) != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    value = 88;

    if (vvector_insert_at(&v, 2, &value) != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    value = 77;

    if (vvector_insert_at(&v, v.size, &value) != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        99, 0, 88, 1, 2, 77
    };

    const int ok =
        expect_int_vector(
            "insert begin/middle/end",
            &v,
            expected,
            sizeof(expected) / sizeof(expected[0])
        );

    vvector_destroy(&v);

    return ok;
}

