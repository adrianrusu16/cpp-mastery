#include <stdio.h>
#include <stddef.h>

#include "vvector.h"


static int expect_vector(
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

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    for (int i = 0; i < 10; ++i) {
        if (vvector_push_back(&v, &i) != 0) {
            vvector_destroy(&v);
            return 0;
        }
    }

    const int expected[] = {
        0, 1, 2, 3, 4,
        5, 6, 7, 8, 9
    };

    const int ok =
        expect_vector(
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

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    for (int i = 0; i < 10; ++i) {
        if (vvector_push_back(&v, &i) != 0) {
            vvector_destroy(&v);
            return 0;
        }
    }

    int value = -1;

    if (vvector_get(&v, 5, &value) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    if (value != 5) {
        vvector_destroy(&v);
        return 0;
    }

    /*
     * Index invalid: ne intereseaza ca functia
     * sa raporteze eroare, nu codul numeric exact.
     */
    if (vvector_get(&v, 99, &value) == 0) {
        vvector_destroy(&v);
        return 0;
    }

    vvector_destroy(&v);

    return 1;
}


static int test_pop_back(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    for (int i = 0; i < 3; ++i) {
        if (vvector_push_back(&v, &i) != 0) {
            vvector_destroy(&v);
            return 0;
        }
    }

    if (vvector_pop_back(&v) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {0, 1};

    const int ok =
        expect_vector(
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

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    /*
     * Pop pe vector gol trebuie sa dea eroare.
     */
    const int result = vvector_pop_back(&v);

    vvector_destroy(&v);

    return result != 0;
}


static int test_shrink_to_fit(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    for (int i = 0; i < 10; ++i) {
        if (vvector_push_back(&v, &i) != 0) {
            vvector_destroy(&v);
            return 0;
        }
    }

    /*
     * size = 10
     * capacity = 16
     */
    if (vvector_pop_back(&v) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    /*
     * size = 9
     * capacity = 16
     */
    if (vvector_shrink_to_fit(&v) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        0, 1, 2, 3, 4,
        5, 6, 7, 8
    };

    const int ok =
        expect_vector(
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

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    for (int i = 0; i < 3; ++i) {
        if (vvector_push_back(&v, &i) != 0) {
            vvector_destroy(&v);
            return 0;
        }
    }

    int value = 99;

    if (vvector_insert_at(&v, 0, &value) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    value = 88;

    if (vvector_insert_at(&v, 2, &value) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    value = 77;

    if (vvector_insert_at(&v, v.size, &value) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        99, 0, 88, 1, 2, 77
    };

    const int ok =
        expect_vector(
            "insert",
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

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    /*
     * Growth:
     *
     * capacity:
     * 0 -> 4
     *
     * Dupa 4 elemente:
     * size == capacity == 4
     */
    for (int i = 0; i < 4; ++i) {
        if (vvector_push_back(&v, &i) != 0) {
            vvector_destroy(&v);
            return 0;
        }
    }

    if (v.size != 4 || v.capacity != 4) {
        vvector_destroy(&v);
        return 0;
    }

    /*
     * source pointeaza DIRECT in storage-ul vectorului.
     *
     * Urmatorul push trebuie sa faca realloc.
     *
     * Dupa apel, source poate fi dangling,
     * deci NU il mai folosim.
     */
    const int *data = (int *)v.data;
    const int *source = &data[1];

    if (vvector_push_back(&v, source) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        0, 1, 2, 3, 1
    };

    const int ok =
        expect_vector(
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

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    /*
     * Rezervam mai mult decat avem nevoie,
     * astfel incat insert-ul sa NU faca realloc.
     */
    if (vvector_reserve(&v, 8) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    for (int i = 0; i < 4; ++i) {
        if (vvector_push_back(&v, &i) != 0) {
            vvector_destroy(&v);
            return 0;
        }
    }

    /*
     * [0, 1, 2, 3]
     *        ^
     *      source
     *
     * Inseram valoarea 2 la index 0.
     *
     * Asta testeaza cazul in care memmove()
     * ar putea modifica memoria catre care
     * pointeaza source.
     */
    const int *data = (int *)v.data;
    const int *source = &data[2];

    if (vvector_insert_at(&v, 0, source) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        2, 0, 1, 2, 3
    };

    const int ok =
        expect_vector(
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

    if (vvector_init(&v, sizeof(int)) != 0) {
        return 0;
    }

    /*
     * Dupa 4 push-uri:
     *
     * size == capacity == 4
     */
    for (int i = 0; i < 4; ++i) {
        if (vvector_push_back(&v, &i) != 0) {
            vvector_destroy(&v);
            return 0;
        }
    }

    if (v.size != 4 || v.capacity != 4) {
        vvector_destroy(&v);
        return 0;
    }

    /*
     * source pointeaza in bufferul care poate
     * fi invalidat de realloc().
     */
    const int *data = (int *)v.data;
    const int *source = &data[2];

    if (vvector_insert_at(&v, 0, source) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    const int expected[] = {
        2, 0, 1, 2, 3
    };

    const int ok =
        expect_vector(
            "self insert with realloc",
            &v,
            expected,
            sizeof(expected) / sizeof(expected[0])
        );

    vvector_destroy(&v);

    return ok;
}


typedef int (*test_function)(void);


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


typedef struct {
    unsigned char payload[512];
    int id;
} LargeObject;


static int test_large_object(void)
{
    vvector v;

    if (vvector_init(&v, sizeof(LargeObject)) != 0) {
        return 0;
    }

    LargeObject object = {0};
    object.id = 1234;

    for (size_t i = 0; i < sizeof(object.payload); ++i) {
        object.payload[i] = (unsigned char)(i % 256);
    }

    if (vvector_push_back(&v, &object) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    LargeObject result = {0};

    if (vvector_get(&v, 0, &result) != 0) {
        vvector_destroy(&v);
        return 0;
    }

    if (result.id != 1234) {
        vvector_destroy(&v);
        return 0;
    }

    for (size_t i = 0; i < sizeof(result.payload); ++i) {
        if (result.payload[i] != (unsigned char)(i % 256)) {
            vvector_destroy(&v);
            return 0;
        }
    }

    vvector_destroy(&v);

    return 1;
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