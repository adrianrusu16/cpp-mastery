#include "test_suites.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <string>
#include <utility>

namespace cpp_mastery::vector_tests {
namespace {

void move_constructor_transfers_allocation()
{
    Vector<std::string> source;
    source.push_back("zero");
    source.push_back("one");
    source.push_back("two");
    source.reserve(8);

    const std::size_t old_size = source.size();
    const std::size_t old_capacity = source.capacity();
    const std::string* old_data = source.data();

    Vector<std::string> destination{std::move(source)};

    REQUIRE_EQ(destination.size(), old_size);
    CHECK_EQ(destination.capacity(), old_capacity);
    CHECK_EQ(destination.data(), old_data);
    CHECK_EQ(destination[0], "zero");
    CHECK_EQ(destination[1], "one");
    CHECK_EQ(destination[2], "two");

    CHECK(source.empty());
    CHECK_EQ(source.size(), 0U);
    CHECK_EQ(source.capacity(), 0U);

    source.push_back("reused");
    REQUIRE_EQ(source.size(), 1U);
    CHECK_EQ(source[0], "reused");
}

void move_assignment_replaces_destination_allocation()
{
    Vector<std::string> source;
    source.push_back("zero");
    source.push_back("one");
    source.push_back("two");
    source.reserve(8);

    const std::size_t source_size = source.size();
    const std::size_t source_capacity = source.capacity();
    const std::string* source_data = source.data();

    Vector<std::string> destination;
    destination.push_back("old-a");
    destination.push_back("old-b");
    destination.reserve(16);

    destination = std::move(source);

    REQUIRE_EQ(destination.size(), source_size);
    CHECK_EQ(destination.capacity(), source_capacity);
    CHECK_EQ(destination.data(), source_data);
    CHECK_EQ(destination[0], "zero");
    CHECK_EQ(destination[1], "one");
    CHECK_EQ(destination[2], "two");

    CHECK(source.empty());
    CHECK_EQ(source.size(), 0U);
    CHECK_EQ(source.capacity(), 0U);

    source.push_back("reused");
    REQUIRE_EQ(source.size(), 1U);
    CHECK_EQ(source[0], "reused");
}

void self_move_assignment_is_safe_noop()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");
    values.reserve(8);

    const std::size_t old_size = values.size();
    const std::size_t old_capacity = values.capacity();
    const std::string* old_data = values.data();

    Vector<std::string>& self = values;
    values = std::move(self);

    REQUIRE_EQ(values.size(), old_size);
    CHECK_EQ(values.capacity(), old_capacity);
    CHECK_EQ(values.data(), old_data);
    CHECK_EQ(values[0], "zero");
    CHECK_EQ(values[1], "one");
    CHECK_EQ(values[2], "two");
}

} // namespace

test::TestSuite make_move_suite()
{
    test::TestSuite suite{"move"};
    suite.add("move constructor transfers allocation", move_constructor_transfers_allocation)
        .add("move assignment replaces destination allocation", move_assignment_replaces_destination_allocation)
        .add("self-move assignment is a safe no-op", self_move_assignment_is_safe_noop);
    return suite;
}

} // namespace cpp_mastery::vector_tests
