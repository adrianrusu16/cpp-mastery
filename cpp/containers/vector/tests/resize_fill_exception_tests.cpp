#include "test_suites.hpp"
#include "test_types.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <stdexcept>

namespace cpp_mastery::vector_tests {
namespace {

void fill_reallocation_rolls_back_fill_copy_failure()
{
    ResizeFillProbe::live_count = 0;
    ResizeFillProbe::destructor_calls = 0;
    ResizeFillProbe::copies = 0;
    ResizeFillProbe::copies_before_throw = -1;

    {
        Vector<ResizeFillProbe> values;
        values.reserve(2);
        values.emplace_back(10);
        values.emplace_back(20);
        ResizeFillProbe fill{99};

        ResizeFillProbe* const old_data = values.data();
        const std::size_t old_capacity = values.capacity();

        ResizeFillProbe::copies_before_throw = 1;
        CHECK_THROWS_AS(values.resize(5, fill), std::runtime_error);
        ResizeFillProbe::copies_before_throw = -1;

        REQUIRE_EQ(values.size(), 2U);
        CHECK_EQ(values.capacity(), old_capacity);
        CHECK_EQ(values.data(), old_data);
        CHECK_EQ(values[0].value, 10);
        CHECK_EQ(values[1].value, 20);
        CHECK_EQ(ResizeFillProbe::live_count, 3);
        CHECK_EQ(ResizeFillProbe::destructor_calls, 1);
    }

    CHECK_EQ(ResizeFillProbe::live_count, 0);
    CHECK_EQ(ResizeFillProbe::destructor_calls, 4);
}

void fill_reallocation_rolls_back_relocation_copy_failure()
{
    ResizeRelocationProbe::live_count = 0;
    ResizeRelocationProbe::destructor_calls = 0;
    ResizeRelocationProbe::copies = 0;
    ResizeRelocationProbe::moves = 0;
    ResizeRelocationProbe::copies_before_throw = -1;

    {
        Vector<ResizeRelocationProbe> values;
        values.reserve(2);
        values.emplace_back(10);
        values.emplace_back(20);
        ResizeRelocationProbe fill{99};

        ResizeRelocationProbe* const old_data = values.data();
        const std::size_t old_capacity = values.capacity();

        ResizeRelocationProbe::copies_before_throw = 4;
        CHECK_THROWS_AS(values.resize(5, fill), std::runtime_error);
        ResizeRelocationProbe::copies_before_throw = -1;

        REQUIRE_EQ(values.size(), 2U);
        CHECK_EQ(values.capacity(), old_capacity);
        CHECK_EQ(values.data(), old_data);
        CHECK_EQ(values[0].value, 10);
        CHECK_EQ(values[1].value, 20);
        CHECK_EQ(ResizeRelocationProbe::copies, 4);
        CHECK_EQ(ResizeRelocationProbe::moves, 0);
        CHECK_EQ(ResizeRelocationProbe::live_count, 3);
        CHECK_EQ(ResizeRelocationProbe::destructor_calls, 4);
    }

    CHECK_EQ(ResizeRelocationProbe::live_count, 0);
    CHECK_EQ(ResizeRelocationProbe::destructor_calls, 7);
}

} // namespace

test::TestSuite make_resize_fill_exception_suite()
{
    test::TestSuite suite{"resize-fill-exceptions"};
    suite.add("fill reallocation rolls back fill-copy failure", fill_reallocation_rolls_back_fill_copy_failure)
        .add("fill reallocation rolls back relocation-copy failure", fill_reallocation_rolls_back_relocation_copy_failure);
    return suite;
}

} // namespace cpp_mastery::vector_tests
