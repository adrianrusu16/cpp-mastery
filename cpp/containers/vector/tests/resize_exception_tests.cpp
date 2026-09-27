#include "test_suites.hpp"
#include "test_types.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <stdexcept>

namespace cpp_mastery::vector_tests {
namespace {

void in_place_growth_rolls_back_default_construction_failure()
{
    ThrowOnDefault::live_count = 0;
    ThrowOnDefault::destructor_calls = 0;
    ThrowOnDefault::defaults_before_throw = -1;

    {
        Vector<ThrowOnDefault> values;
        values.reserve(5);
        values.emplace_back(10);
        values.emplace_back(20);

        REQUIRE_EQ(values.size(), 2U);
        REQUIRE_EQ(ThrowOnDefault::live_count, 2);

        const std::size_t old_capacity = values.capacity();
        ThrowOnDefault* const old_data = values.data();

        ThrowOnDefault::defaults_before_throw = 1;
        CHECK_THROWS_AS(values.resize(5), std::runtime_error);
        ThrowOnDefault::defaults_before_throw = -1;

        REQUIRE_EQ(values.size(), 2U);
        CHECK_EQ(values.capacity(), old_capacity);
        CHECK_EQ(values.data(), old_data);
        CHECK_EQ(values[0].value, 10);
        CHECK_EQ(values[1].value, 20);
        CHECK_EQ(ThrowOnDefault::live_count, 2);
        CHECK_EQ(ThrowOnDefault::destructor_calls, 1);
    }

    CHECK_EQ(ThrowOnDefault::live_count, 0);
    CHECK_EQ(ThrowOnDefault::destructor_calls, 3);
}

void reallocation_rolls_back_default_construction_failure()
{
    ThrowOnDefault::live_count = 0;
    ThrowOnDefault::destructor_calls = 0;
    ThrowOnDefault::defaults_before_throw = -1;

    {
        Vector<ThrowOnDefault> values;
        values.reserve(2);
        values.emplace_back(10);
        values.emplace_back(20);

        ThrowOnDefault* const old_data = values.data();
        const std::size_t old_capacity = values.capacity();

        ThrowOnDefault::defaults_before_throw = 1;
        CHECK_THROWS_AS(values.resize(5), std::runtime_error);
        ThrowOnDefault::defaults_before_throw = -1;

        REQUIRE_EQ(values.size(), 2U);
        CHECK_EQ(values.capacity(), old_capacity);
        CHECK_EQ(values.data(), old_data);
        CHECK_EQ(values[0].value, 10);
        CHECK_EQ(values[1].value, 20);
        CHECK_EQ(ThrowOnDefault::live_count, 2);
        CHECK_EQ(ThrowOnDefault::destructor_calls, 1);
    }

    CHECK_EQ(ThrowOnDefault::live_count, 0);
    CHECK_EQ(ThrowOnDefault::destructor_calls, 3);
}

void reallocation_rolls_back_relocation_copy_failure()
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

        ResizeRelocationProbe* const old_data = values.data();
        const std::size_t old_capacity = values.capacity();

        ResizeRelocationProbe::copies_before_throw = 1;
        CHECK_THROWS_AS(values.resize(5), std::runtime_error);
        ResizeRelocationProbe::copies_before_throw = -1;

        REQUIRE_EQ(values.size(), 2U);
        CHECK_EQ(values.capacity(), old_capacity);
        CHECK_EQ(values.data(), old_data);
        CHECK_EQ(values[0].value, 10);
        CHECK_EQ(values[1].value, 20);
        CHECK_EQ(ResizeRelocationProbe::copies, 1);
        CHECK_EQ(ResizeRelocationProbe::moves, 0);
        CHECK_EQ(ResizeRelocationProbe::live_count, 2);
        CHECK_EQ(ResizeRelocationProbe::destructor_calls, 4);
    }

    CHECK_EQ(ResizeRelocationProbe::live_count, 0);
    CHECK_EQ(ResizeRelocationProbe::destructor_calls, 6);
}

} // namespace

test::TestSuite make_resize_exception_suite()
{
    test::TestSuite suite{"resize-exceptions"};
    suite.add("in-place growth rolls back default-construction failure", in_place_growth_rolls_back_default_construction_failure)
        .add("reallocation rolls back default-construction failure", reallocation_rolls_back_default_construction_failure)
        .add("reallocation rolls back relocation-copy failure", reallocation_rolls_back_relocation_copy_failure);
    return suite;
}

} // namespace cpp_mastery::vector_tests
