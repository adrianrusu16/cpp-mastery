#include "test_support.h"
#include "test_cases.h"

int test_self_push_back_with_realloc(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    for (int i = 0; i < 4; ++i) {
        if (vvector_push_back(&v, &i) != VVECTOR_OK) {
            vvector_destroy(&v);
            return 0;
        }
    }

    if (v.size != 4 || v.capacity != 4) {
        vvector_destroy(&v);
        return 0;
    }

    const int *data =
        (const int *)v.data;

    const int *source =
        &data[1];

    /*
     * source may become dangling after this call.
     * Do not use it afterwards.
     */
    const vvector_result result =
        vvector_push_back(&v, source);

    if (result != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        0, 1, 2, 3, 1
    };

    const int ok =
        expect_int_vector(
            "self push_back with realloc",
            &v,
            expected,
            sizeof(expected) / sizeof(expected[0])
        );

    vvector_destroy(&v);

    return ok;
}

int test_self_insert_without_realloc(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    if (vvector_reserve(&v, 8) != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    for (int i = 0; i < 4; ++i) {
        if (vvector_push_back(&v, &i) != VVECTOR_OK) {
            vvector_destroy(&v);
            return 0;
        }
    }

    const int *data =
        (const int *)v.data;

    const int *source =
        &data[2];

    const vvector_result result =
        vvector_insert_at(&v, 0, source);

    if (result != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        2, 0, 1, 2, 3
    };

    const int ok =
        expect_int_vector(
            "self insert without realloc",
            &v,
            expected,
            sizeof(expected) / sizeof(expected[0])
        )
        && v.capacity == 8;

    vvector_destroy(&v);

    return ok;
}

int test_self_insert_with_realloc(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    for (int i = 0; i < 4; ++i) {
        if (vvector_push_back(&v, &i) != VVECTOR_OK) {
            vvector_destroy(&v);
            return 0;
        }
    }

    if (v.size != 4 || v.capacity != 4) {
        vvector_destroy(&v);
        return 0;
    }

    const int *data =
        (const int *)v.data;

    const int *source =
        &data[2];

    /*
     * The insert must grow the buffer.
     * source may therefore become dangling during the call.
     */
    const vvector_result result =
        vvector_insert_at(&v, 0, source);

    if (result != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        2, 0, 1, 2, 3
    };

    const int ok =
        expect_int_vector(
            "self insert with realloc",
            &v,
            expected,
            sizeof(expected) / sizeof(expected[0])
        );

    vvector_destroy(&v);

    return ok;
}

int test_large_object(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(LargeObject)) != VVECTOR_OK)
        return 0;

    LargeObject object = {0};
    object.id = 1234;

    for (size_t i = 0; i < sizeof(object.payload); ++i) {
        object.payload[i] =
            (unsigned char)(i % 256);
    }

    if (vvector_push_back(&v, &object) != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    LargeObject result = {0};

    if (vvector_get(&v, 0, &result) != VVECTOR_OK) {
        vvector_destroy(&v);
        return 0;
    }

    if (result.id != 1234) {
        vvector_destroy(&v);
        return 0;
    }

    for (size_t i = 0; i < sizeof(result.payload); ++i) {
        const unsigned char expected =
            (unsigned char)(i % 256);

        if (result.payload[i] != expected) {
            vvector_destroy(&v);
            return 0;
        }
    }

    vvector_destroy(&v);

    return 1;
}

