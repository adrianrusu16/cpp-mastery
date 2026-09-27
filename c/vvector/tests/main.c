#include "TestRunner.h"
#include "test_cases.h"

#define TEST_CASE(name) { #name, name }

int main(void)
{
    const cpp_mastery_test_case tests[] = {
        {"push_back grows geometrically", test_push_normal},
        {"get handles valid and invalid indexes", test_get},
        {"pop_back removes the last element", test_pop_back},
        {"pop_back rejects an empty vector", test_pop_back_empty},
        {"shrink_to_fit releases spare capacity", test_shrink_to_fit},
        {"insert handles begin, middle, and end", test_insert},
        {"self push_back survives reallocation", test_self_push_back_with_realloc},
        {"self insert works without reallocation", test_self_insert_without_realloc},
        {"self insert survives reallocation", test_self_insert_with_realloc},
        {"large elements use the fallback temporary buffer", test_large_object},
    };

    return cpp_mastery_run_tests(tests, sizeof(tests) / sizeof(tests[0]));
}
