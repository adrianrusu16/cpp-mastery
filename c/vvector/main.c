#include <stdio.h>

#include "vvector.h"


typedef struct {
    unsigned char payload[512];
    int id;
} LargeObject;


typedef int (*test_function)(void);


static int expect_int_vector(
    const char *test_name,
    const vvector *v,
    const int *expected,
    const size_t expected_size)
{
    if (v->size != expected_size) {
        printf(
            "    FAIL: %s - expected size %zu, got %zu\n",
            test_name,
            expected_size,
            v->size
        );

        return 0;
    }

    const int *data = (const int *)v->data;

    for (size_t i = 0; i < expected_size; ++i) {
        if (data[i] != expected[i]) {
            printf(
                "    FAIL: %s - index %zu: expected %d, got %d\n",
                test_name,
                i,
                expected[i],
                data[i]
            );

            return 0;
        }
    }

    return 1;
}


static int test_push_normal(void)
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


static int test_get(void)
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


static int test_pop_back(void)
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


static int test_pop_back_empty(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != VVECTOR_OK)
        return 0;

    const vvector_result result =
        vvector_pop_back(&v);

    vvector_destroy(&v);

    return result == VVECTOR_ERROR_OUT_OF_BOUNDS;
}


static int test_shrink_to_fit(void)
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


static int test_insert(void)
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


static int test_self_push_back_with_realloc(void)
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


static int test_self_insert_without_realloc(void)
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


static int test_self_insert_with_realloc(void)
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


static int test_large_object(void)
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


static void run_test(
    const char *name,
    const test_function test,
    size_t *passed,
    size_t *failed)
{
    printf("%-40s", name);

    if (test()) {
        printf("PASS\n");
        (*passed)++;
    } else {
        printf("FAIL\n");
        (*failed)++;
    }
}


int main(void)
{
    size_t passed = 0;
    size_t failed = 0;

    run_test(
        "push normal",
        test_push_normal,
        &passed,
        &failed
    );

    run_test(
        "get valid + invalid",
        test_get,
        &passed,
        &failed
    );

    run_test(
        "pop_back",
        test_pop_back,
        &passed,
        &failed
    );

    run_test(
        "pop_back on empty vector",
        test_pop_back_empty,
        &passed,
        &failed
    );

    run_test(
        "shrink_to_fit",
        test_shrink_to_fit,
        &passed,
        &failed
    );

    run_test(
        "insert begin/middle/end",
        test_insert,
        &passed,
        &failed
    );

    run_test(
        "SELF push_back + realloc",
        test_self_push_back_with_realloc,
        &passed,
        &failed
    );

    run_test(
        "SELF insert without realloc",
        test_self_insert_without_realloc,
        &passed,
        &failed
    );

    run_test(
        "SELF insert + realloc",
        test_self_insert_with_realloc,
        &passed,
        &failed
    );

    run_test(
        "large object",
        test_large_object,
        &passed,
        &failed
    );

    printf("\n");
    printf("========================================\n");
    printf("Passed: %zu\n", passed);
    printf("Failed: %zu\n", failed);
    printf("========================================\n");

    return failed == 0 ? 0 : 1;
}