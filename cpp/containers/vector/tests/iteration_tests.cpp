#include "test_suites.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <algorithm>
#include <ranges>

namespace cpp_mastery::vector_tests {

static_assert(std::ranges::range<Vector<int>>);
static_assert(std::ranges::sized_range<Vector<int>>);
static_assert(std::ranges::common_range<Vector<int>>);
static_assert(std::ranges::random_access_range<Vector<int>>);
static_assert(std::ranges::contiguous_range<Vector<int>>);

namespace {

void begin_end_expose_mutable_storage()
{
    Vector<int> values;
    values.push_back(10);
    values.push_back(20);
    values.push_back(30);

    int* first = values.begin();
    int* last = values.end();

    REQUIRE_EQ(first, values.data());
    CHECK_EQ(last, values.data() + values.size());
    CHECK_EQ(last - first, 3);
    CHECK_EQ(*first, 10);

    *(first + 1) = 99;
    CHECK_EQ(values[1], 99);
}

void begin_end_match_for_empty_vector()
{
    Vector<int> values;

    CHECK_EQ(values.begin(), nullptr);
    CHECK_EQ(values.end(), nullptr);
    CHECK_EQ(values.begin(), values.end());
}

void begin_end_preserve_constness()
{
    Vector<int> mutable_values;
    mutable_values.push_back(10);
    mutable_values.push_back(20);
    mutable_values.push_back(30);

    const Vector<int>& values = mutable_values;
    const int* first = values.begin();
    const int* last = values.end();

    REQUIRE_EQ(first, values.data());
    CHECK_EQ(last, values.data() + values.size());
    CHECK_EQ(last - first, 3);
    CHECK_EQ(*first, 10);
}

void range_based_for_iterates_all_elements()
{
    Vector<int> values;
    values.push_back(10);
    values.push_back(20);
    values.push_back(30);

    int sum = 0;
    for (const int value : values) {
        sum += value;
    }

    CHECK_EQ(sum, 60);
}

void std_sort_accepts_vector_iterators()
{
    Vector<int> values;
    values.push_back(40);
    values.push_back(10);
    values.push_back(30);
    values.push_back(20);

    std::sort(values.begin(), values.end());

    CHECK_EQ(values[0], 10);
    CHECK_EQ(values[1], 20);
    CHECK_EQ(values[2], 30);
    CHECK_EQ(values[3], 40);
}

void const_range_based_for_uses_const_iterators()
{
    Vector<int> mutable_values;
    mutable_values.push_back(10);
    mutable_values.push_back(20);
    mutable_values.push_back(30);

    const Vector<int>& values = mutable_values;
    int sum = 0;
    for (const int value : values) {
        sum += value;
    }

    CHECK_EQ(sum, 60);
}

void ranges_algorithms_recognize_vector()
{
    Vector<int> values;
    values.push_back(40);
    values.push_back(10);
    values.push_back(30);
    values.push_back(20);

    CHECK_EQ(std::ranges::size(values), 4U);
    CHECK_EQ(std::ranges::data(values), values.data());

    std::ranges::sort(values);

    CHECK_EQ(values[0], 10);
    CHECK_EQ(values[1], 20);
    CHECK_EQ(values[2], 30);
    CHECK_EQ(values[3], 40);
}

} // namespace

test::TestSuite make_iteration_suite()
{
    test::TestSuite suite{"iteration"};
    suite.add("begin/end expose mutable storage", begin_end_expose_mutable_storage)
        .add("begin/end match for empty vector", begin_end_match_for_empty_vector)
        .add("begin/end preserve constness", begin_end_preserve_constness)
        .add("range-based for iterates all elements", range_based_for_iterates_all_elements)
        .add("std::sort accepts vector iterators", std_sort_accepts_vector_iterators)
        .add("const range-based for uses const iterators", const_range_based_for_uses_const_iterators)
        .add("ranges algorithms recognize vector", ranges_algorithms_recognize_vector);
    return suite;
}

} // namespace cpp_mastery::vector_tests
