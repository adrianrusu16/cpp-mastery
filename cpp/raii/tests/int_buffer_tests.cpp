#include "test_suites.hpp"

#include <cpp_mastery/raii/IntBuffer.hpp>
#include <cpp_mastery/testing/Testing.hpp>

#include <utility>

namespace cpp_mastery::raii_tests {
namespace {

using cpp_mastery::IntBuffer;

void construction_exposes_owned_elements()
{
    IntBuffer buffer{3};
    REQUIRE_EQ(buffer.size(), 3U);

    buffer[0] = 10;
    buffer[1] = 20;
    buffer[2] = 30;

    CHECK_EQ(buffer[0], 10);
    CHECK_EQ(buffer[1], 20);
    CHECK_EQ(buffer[2], 30);
}

void copy_construction_owns_independent_storage()
{
    IntBuffer original{3};
    original[0] = 10;
    original[1] = 20;
    original[2] = 30;

    IntBuffer copy{original};

    REQUIRE_EQ(copy.size(), original.size());
    CHECK_EQ(copy[0], 10);
    CHECK_EQ(copy[1], 20);
    CHECK_EQ(copy[2], 30);

    copy[0] = 999;
    CHECK_EQ(original[0], 10);
    CHECK_EQ(copy[0], 999);
}

void copy_assignment_replaces_value_independently()
{
    IntBuffer source{3};
    source[0] = 10;
    source[1] = 20;
    source[2] = 30;

    IntBuffer destination{5};
    destination[0] = 100;
    destination[1] = 200;

    destination = source;

    REQUIRE_EQ(destination.size(), 3U);
    CHECK_EQ(destination[0], 10);
    CHECK_EQ(destination[1], 20);
    CHECK_EQ(destination[2], 30);

    destination[1] = 999;
    CHECK_EQ(source[1], 20);
    CHECK_EQ(destination[1], 999);
}

void move_construction_transfers_value_and_empties_source()
{
    IntBuffer source{3};
    source[0] = 10;
    source[1] = 20;
    source[2] = 30;

    IntBuffer destination{std::move(source)};

    REQUIRE_EQ(destination.size(), 3U);
    CHECK_EQ(destination[0], 10);
    CHECK_EQ(destination[1], 20);
    CHECK_EQ(destination[2], 30);
    CHECK_EQ(source.size(), 0U);
}

void move_assignment_transfers_value_and_empties_source()
{
    IntBuffer source{3};
    source[0] = 10;
    source[1] = 20;
    source[2] = 30;

    IntBuffer destination{5};
    destination[0] = 100;
    destination[1] = 200;

    destination = std::move(source);

    REQUIRE_EQ(destination.size(), 3U);
    CHECK_EQ(destination[0], 10);
    CHECK_EQ(destination[1], 20);
    CHECK_EQ(destination[2], 30);
    CHECK_EQ(source.size(), 0U);
}

void self_copy_assignment_preserves_value()
{
    IntBuffer buffer{3};
    buffer[0] = 10;
    buffer[1] = 20;
    buffer[2] = 30;

    const IntBuffer& self = buffer;
    buffer = self;

    REQUIRE_EQ(buffer.size(), 3U);
    CHECK_EQ(buffer[0], 10);
    CHECK_EQ(buffer[1], 20);
    CHECK_EQ(buffer[2], 30);
}

void self_move_assignment_leaves_object_reusable()
{
    IntBuffer buffer{3};
    buffer[0] = 10;
    buffer[1] = 20;
    buffer[2] = 30;

    IntBuffer& self = buffer;
    buffer = std::move(self);

    buffer = IntBuffer{2};
    buffer[0] = 42;
    buffer[1] = 84;

    REQUIRE_EQ(buffer.size(), 2U);
    CHECK_EQ(buffer[0], 42);
    CHECK_EQ(buffer[1], 84);
}

void moved_from_buffer_can_be_assigned_again()
{
    IntBuffer source{3};
    source[0] = 10;
    source[1] = 20;
    source[2] = 30;

    IntBuffer destination{std::move(source)};

    IntBuffer replacement{2};
    replacement[0] = 42;
    replacement[1] = 84;
    source = replacement;

    REQUIRE_EQ(source.size(), 2U);
    CHECK_EQ(source[0], 42);
    CHECK_EQ(source[1], 84);
    REQUIRE_EQ(destination.size(), 3U);
    CHECK_EQ(destination[0], 10);
}

} // namespace

test::TestSuite make_int_buffer_suite()
{
    test::TestSuite suite{"int-buffer"};
    suite.add("construction exposes owned elements", construction_exposes_owned_elements)
        .add("copy construction owns independent storage", copy_construction_owns_independent_storage)
        .add("copy assignment replaces value independently", copy_assignment_replaces_value_independently)
        .add("move construction transfers value and empties source", move_construction_transfers_value_and_empties_source)
        .add("move assignment transfers value and empties source", move_assignment_transfers_value_and_empties_source)
        .add("self-copy assignment preserves value", self_copy_assignment_preserves_value)
        .add("self-move assignment leaves object reusable", self_move_assignment_leaves_object_reusable)
        .add("moved-from buffer can be assigned again", moved_from_buffer_can_be_assigned_again);
    return suite;
}

} // namespace cpp_mastery::raii_tests
