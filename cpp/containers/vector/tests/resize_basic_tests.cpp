#include "test_suites.hpp"
#include "test_types.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <string>

namespace cpp_mastery::vector_tests {
namespace {

void resize_shrinks_without_reallocating()
{
    Vector<std::string> values;
    values.reserve(8);
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");
    values.push_back("three");

    const std::size_t old_capacity = values.capacity();
    values.resize(2);

    REQUIRE_EQ(values.size(), 2U);
    CHECK_EQ(values.capacity(), old_capacity);
    CHECK_EQ(values[0], "zero");
    CHECK_EQ(values[1], "one");
}

void resize_shrink_destroys_removed_elements()
{
    ResizeProbe::live_count = 0;
    ResizeProbe::destructor_calls = 0;

    {
        Vector<ResizeProbe> values;
        values.reserve(4);
        values.emplace_back(10);
        values.emplace_back(20);
        values.emplace_back(30);
        values.emplace_back(40);

        REQUIRE_EQ(ResizeProbe::live_count, 4);
        CHECK_EQ(ResizeProbe::destructor_calls, 0);

        values.resize(2);

        REQUIRE_EQ(values.size(), 2U);
        CHECK_EQ(ResizeProbe::live_count, 2);
        CHECK_EQ(ResizeProbe::destructor_calls, 2);
        CHECK_EQ(values[0].value, 10);
        CHECK_EQ(values[1].value, 20);
    }

    CHECK_EQ(ResizeProbe::live_count, 0);
    CHECK_EQ(ResizeProbe::destructor_calls, 4);
}

void resize_grows_in_place_when_capacity_is_available()
{
    Vector<int> values;
    values.reserve(8);
    values.push_back(10);
    values.push_back(20);

    int* const old_data = values.data();
    const std::size_t old_capacity = values.capacity();

    values.resize(5);

    REQUIRE_EQ(values.size(), 5U);
    CHECK_EQ(values.capacity(), old_capacity);
    CHECK_EQ(values.data(), old_data);
    CHECK_EQ(values[0], 10);
    CHECK_EQ(values[1], 20);
    CHECK_EQ(values[2], 0);
    CHECK_EQ(values[3], 0);
    CHECK_EQ(values[4], 0);
}

void resize_reallocates_when_capacity_is_insufficient()
{
    Vector<int> values;
    values.push_back(10);
    values.push_back(20);

    int* const old_data = values.data();
    values.resize(10);

    REQUIRE_EQ(values.size(), 10U);
    CHECK(values.capacity() >= 10U);
    CHECK_NE(values.data(), old_data);
    CHECK_EQ(values[0], 10);
    CHECK_EQ(values[1], 20);

    for (std::size_t index = 2; index < values.size(); ++index) {
        CHECK_EQ(values[index], 0);
    }
}

} // namespace

test::TestSuite make_resize_suite()
{
    test::TestSuite suite{"resize"};
    suite.add("shrinks without reallocating", resize_shrinks_without_reallocating)
        .add("shrink destroys removed elements", resize_shrink_destroys_removed_elements)
        .add("grows in place when capacity is available", resize_grows_in_place_when_capacity_is_available)
        .add("reallocates when capacity is insufficient", resize_reallocates_when_capacity_is_insufficient);
    return suite;
}

} // namespace cpp_mastery::vector_tests
