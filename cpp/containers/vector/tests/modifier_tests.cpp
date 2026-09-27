#include "test_suites.hpp"
#include "test_types.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <string>
#include <utility>

namespace cpp_mastery::vector_tests {
namespace {

void emplace_back_constructs_in_place()
{
    EmplaceProbe::direct_constructions = 0;
    EmplaceProbe::copies = 0;
    EmplaceProbe::moves = 0;

    Vector<EmplaceProbe> values;
    EmplaceProbe& element = values.emplace_back(42, std::string{"engine"});

    REQUIRE_EQ(values.size(), 1U);
    CHECK_EQ(element.id, 42);
    CHECK_EQ(element.name, "engine");
    CHECK_EQ(&element, &values[0]);
    CHECK_EQ(EmplaceProbe::direct_constructions, 1);
    CHECK_EQ(EmplaceProbe::copies, 0);
    CHECK_EQ(EmplaceProbe::moves, 0);
}

void push_back_preserves_self_copy_during_reallocation()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");
    values.push_back("three");

    REQUIRE_EQ(values.size(), 4U);
    REQUIRE_EQ(values.capacity(), 4U);

    values.push_back(values[0]);

    REQUIRE_EQ(values.size(), 5U);
    CHECK_EQ(values.capacity(), 8U);
    CHECK_EQ(values[0], "zero");
    CHECK_EQ(values[1], "one");
    CHECK_EQ(values[2], "two");
    CHECK_EQ(values[3], "three");
    CHECK_EQ(values[4], "zero");
}

void push_back_supports_self_move_during_reallocation()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");
    values.push_back("three");

    REQUIRE_EQ(values.size(), 4U);
    REQUIRE_EQ(values.capacity(), 4U);

    values.push_back(std::move(values[0]));

    REQUIRE_EQ(values.size(), 5U);
    CHECK_EQ(values.capacity(), 8U);
    CHECK_EQ(values[1], "one");
    CHECK_EQ(values[2], "two");
    CHECK_EQ(values[3], "three");
    CHECK_EQ(values[4], "zero");
}

void pop_back_removes_last_element_without_shrinking_capacity()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");

    const std::size_t old_capacity = values.capacity();
    values.pop_back();

    REQUIRE_EQ(values.size(), 2U);
    CHECK_EQ(values.capacity(), old_capacity);
    CHECK_EQ(values[0], "zero");
    CHECK_EQ(values[1], "one");
    CHECK_EQ(values.back(), "one");
}

void pop_back_destroys_removed_element()
{
    PopBackProbe::live_count = 0;
    PopBackProbe::destructor_calls = 0;

    {
        Vector<PopBackProbe> values;
        values.reserve(3);
        values.emplace_back(10);
        values.emplace_back(20);
        values.emplace_back(30);

        REQUIRE_EQ(PopBackProbe::live_count, 3);
        CHECK_EQ(PopBackProbe::destructor_calls, 0);

        values.pop_back();

        REQUIRE_EQ(values.size(), 2U);
        CHECK_EQ(PopBackProbe::live_count, 2);
        CHECK_EQ(PopBackProbe::destructor_calls, 1);
        CHECK_EQ(values.back().value, 20);
    }

    CHECK_EQ(PopBackProbe::live_count, 0);
    CHECK_EQ(PopBackProbe::destructor_calls, 3);
}

} // namespace

test::TestSuite make_modifier_suite()
{
    test::TestSuite suite{"modifiers"};
    suite.add("emplace_back constructs in place", emplace_back_constructs_in_place)
        .add("push_back preserves self-copy during reallocation", push_back_preserves_self_copy_during_reallocation)
        .add("push_back supports self-move during reallocation", push_back_supports_self_move_during_reallocation)
        .add("pop_back removes last element without shrinking capacity", pop_back_removes_last_element_without_shrinking_capacity)
        .add("pop_back destroys removed element", pop_back_destroys_removed_element);
    return suite;
}

} // namespace cpp_mastery::vector_tests
