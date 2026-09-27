#ifndef CPP_MASTERY_TEST_RUNNER_H
#define CPP_MASTERY_TEST_RUNNER_H

#include <stddef.h>
#include <stdio.h>

typedef int (*cpp_mastery_test_function)(void);

typedef struct {
    const char *name;
    cpp_mastery_test_function function;
} cpp_mastery_test_case;

static inline int cpp_mastery_run_tests(
    const cpp_mastery_test_case *tests,
    const size_t count)
{
    size_t passed = 0;
    size_t failed = 0;

    for (size_t i = 0; i < count; ++i) {
        const int success = tests[i].function();
        printf("[%s] %s\n", success ? "PASS" : "FAIL", tests[i].name);

        if (success) {
            ++passed;
        } else {
            ++failed;
        }
    }

    printf("\n%zu passed, %zu failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

#endif
