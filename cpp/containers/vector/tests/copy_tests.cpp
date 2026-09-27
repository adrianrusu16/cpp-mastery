#include "test_suites.hpp"
#include "test_types.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <stdexcept>
#include <string>

namespace cpp_mastery::vector_tests {
namespace {

void copy_constructor_copies_only_live_elements()
{
    Vector<std::string> original;
    original.push_back("zero");
    original.push_back("one");
    original.push_back("two");
    original.reserve(8);

    Vector<std::string> copy{original};

    REQUIRE_EQ(original.size(), 3U);
    CHECK_EQ(original.capacity(), 8U);
    REQUIRE_EQ(copy.size(), 3U);
    CHECK_EQ(copy.capacity(), 3U);
    CHECK_EQ(copy[0], "zero");
    CHECK_EQ(copy[1], "one");
    CHECK_EQ(copy[2], "two");

    copy[0] = "changed";
    CHECK_EQ(copy[0], "changed");
    CHECK_EQ(original[0], "zero");
}

void copy_constructor_rolls_back_partial_copy()
{
    REQUIRE_EQ(ThrowOnCopy::live_count, 0);

    {
        Vector<ThrowOnCopy> original;
        original.push_back(ThrowOnCopy{10});
        original.push_back(ThrowOnCopy{20});
        original.push_back(ThrowOnCopy{30});
        original.push_back(ThrowOnCopy{40});
        original.reserve(8);

        REQUIRE_EQ(ThrowOnCopy::live_count, 4);
        const int live_before_copy = ThrowOnCopy::live_count;

        ThrowOnCopy::copies_before_throw = 2;
        CHECK_THROWS_AS(Vector<ThrowOnCopy>{original}, std::runtime_error);
        ThrowOnCopy::copies_before_throw = -1;

        CHECK_EQ(ThrowOnCopy::live_count, live_before_copy);
        REQUIRE_EQ(original.size(), 4U);
        CHECK_EQ(original[0].value, 10);
        CHECK_EQ(original[1].value, 20);
        CHECK_EQ(original[2].value, 30);
        CHECK_EQ(original[3].value, 40);
    }

    CHECK_EQ(ThrowOnCopy::live_count, 0);
}

void copy_assignment_replaces_value_with_deep_copy()
{
    Vector<std::string> source;
    source.push_back("zero");
    source.push_back("one");
    source.push_back("two");
    source.reserve(8);

    Vector<std::string> destination;
    destination.push_back("old-a");
    destination.push_back("old-b");
    destination.reserve(16);

    destination = source;

    REQUIRE_EQ(source.size(), 3U);
    CHECK_EQ(source.capacity(), 8U);
    CHECK_EQ(source[0], "zero");
    CHECK_EQ(source[1], "one");
    CHECK_EQ(source[2], "two");

    REQUIRE_EQ(destination.size(), 3U);
    CHECK_EQ(destination.capacity(), 3U);
    CHECK_EQ(destination[0], "zero");
    CHECK_EQ(destination[1], "one");
    CHECK_EQ(destination[2], "two");

    destination[0] = "changed";
    CHECK_EQ(source[0], "zero");
    CHECK_EQ(destination[0], "changed");
}

void self_copy_assignment_preserves_storage_and_values()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");
    values.reserve(8);

    const std::size_t old_size = values.size();
    const std::size_t old_capacity = values.capacity();
    const Vector<std::string>& self = values;

    values = self;

    REQUIRE_EQ(values.size(), old_size);
    CHECK_EQ(values.capacity(), old_capacity);
    CHECK_EQ(values[0], "zero");
    CHECK_EQ(values[1], "one");
    CHECK_EQ(values[2], "two");
}

void copy_assignment_has_strong_exception_guarantee()
{
    REQUIRE_EQ(ThrowOnCopy::live_count, 0);

    {
        Vector<ThrowOnCopy> source;
        source.push_back(ThrowOnCopy{10});
        source.push_back(ThrowOnCopy{20});
        source.push_back(ThrowOnCopy{30});
        source.push_back(ThrowOnCopy{40});
        source.reserve(8);

        Vector<ThrowOnCopy> destination;
        destination.push_back(ThrowOnCopy{100});
        destination.push_back(ThrowOnCopy{200});
        destination.reserve(16);

        const std::size_t old_size = destination.size();
        const std::size_t old_capacity = destination.capacity();
        const int live_before_assignment = ThrowOnCopy::live_count;

        ThrowOnCopy::copies_before_throw = 2;
        CHECK_THROWS_AS(destination = source, std::runtime_error);
        ThrowOnCopy::copies_before_throw = -1;

        CHECK_EQ(ThrowOnCopy::live_count, live_before_assignment);
        REQUIRE_EQ(source.size(), 4U);
        CHECK_EQ(source.capacity(), 8U);
        CHECK_EQ(source[0].value, 10);
        CHECK_EQ(source[1].value, 20);
        CHECK_EQ(source[2].value, 30);
        CHECK_EQ(source[3].value, 40);

        REQUIRE_EQ(destination.size(), old_size);
        CHECK_EQ(destination.capacity(), old_capacity);
        CHECK_EQ(destination[0].value, 100);
        CHECK_EQ(destination[1].value, 200);
    }

    CHECK_EQ(ThrowOnCopy::live_count, 0);
}

} // namespace

test::TestSuite make_copy_suite()
{
    test::TestSuite suite{"copy"};
    suite.add("copy constructor copies only live elements", copy_constructor_copies_only_live_elements)
        .add("copy constructor rolls back partial copy", copy_constructor_rolls_back_partial_copy)
        .add("copy assignment replaces value with deep copy", copy_assignment_replaces_value_with_deep_copy)
        .add("self-copy assignment preserves storage and values", self_copy_assignment_preserves_storage_and_values)
        .add("copy assignment has strong exception guarantee", copy_assignment_has_strong_exception_guarantee);
    return suite;
}

} // namespace cpp_mastery::vector_tests
