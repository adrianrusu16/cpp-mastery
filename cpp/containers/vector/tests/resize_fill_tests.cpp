#include "test_suites.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <string>

namespace cpp_mastery::vector_tests {
namespace {

void resize_fill_grows_in_place()
{
    Vector<int> values;
    values.reserve(8);
    values.push_back(10);
    values.push_back(20);

    int* const old_data = values.data();
    const std::size_t old_capacity = values.capacity();

    values.resize(5, 99);

    REQUIRE_EQ(values.size(), 5U);
    CHECK_EQ(values.capacity(), old_capacity);
    CHECK_EQ(values.data(), old_data);
    CHECK_EQ(values[0], 10);
    CHECK_EQ(values[1], 20);
    CHECK_EQ(values[2], 99);
    CHECK_EQ(values[3], 99);
    CHECK_EQ(values[4], 99);
}

void resize_fill_supports_alias_in_place()
{
    Vector<std::string> values;
    values.reserve(8);
    values.push_back("zero");
    values.push_back("one");

    values.resize(5, values[0]);

    REQUIRE_EQ(values.size(), 5U);
    CHECK_EQ(values[0], "zero");
    CHECK_EQ(values[1], "one");
    CHECK_EQ(values[2], "zero");
    CHECK_EQ(values[3], "zero");
    CHECK_EQ(values[4], "zero");
}

void resize_fill_reallocates_and_copies_value()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");

    std::string* const old_data = values.data();
    values.resize(10, std::string{"fill"});

    REQUIRE_EQ(values.size(), 10U);
    CHECK(values.capacity() >= 10U);
    CHECK_NE(values.data(), old_data);
    CHECK_EQ(values[0], "zero");
    CHECK_EQ(values[1], "one");

    for (std::size_t index = 2; index < values.size(); ++index) {
        CHECK_EQ(values[index], "fill");
    }
}

void resize_fill_preserves_alias_during_reallocation()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");

    std::string* const old_data = values.data();
    values.resize(10, values[0]);

    REQUIRE_EQ(values.size(), 10U);
    CHECK(values.capacity() >= 10U);
    CHECK_NE(values.data(), old_data);
    CHECK_EQ(values[0], "zero");
    CHECK_EQ(values[1], "one");

    for (std::size_t index = 2; index < values.size(); ++index) {
        CHECK_EQ(values[index], "zero");
    }
}

} // namespace

test::TestSuite make_resize_fill_suite()
{
    test::TestSuite suite{"resize-fill"};
    suite.add("grows in place", resize_fill_grows_in_place)
        .add("supports alias in place", resize_fill_supports_alias_in_place)
        .add("reallocates and copies fill value", resize_fill_reallocates_and_copies_value)
        .add("preserves alias during reallocation", resize_fill_preserves_alias_during_reallocation);
    return suite;
}

} // namespace cpp_mastery::vector_tests
