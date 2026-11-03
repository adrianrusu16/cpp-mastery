#include <cpp_mastery/parsing/ParseZeroBasedIndex.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{

using cpp_mastery::parsing::parse_zero_based_index;
using cpp_mastery::parsing::ParseError;

static_assert(
    std::is_same_v<decltype(parse_zero_based_index("1")), std::expected<int, ParseError>>);
static_assert(!std::is_constructible_v<std::expected<int, ParseError>, ParseError>);

void expect_success(const std::string_view input, const int expected)
{
    const auto result = parse_zero_based_index(input);
    REQUIRE(result.has_value());
    CHECK_EQ(*result, expected);
}

void expect_failure(const std::string_view input, const ParseError error)
{
    const auto result = parse_zero_based_index(input);
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error(), error);
}

void one_maps_to_zero()
{
    expect_success("1", 0);
}

void decimal_index_maps_to_zero_based()
{
    expect_success("42", 41);
}

void leading_zeroes_preserve_value()
{
    expect_success("0042", 41);
}

void maximum_integer_does_not_overflow()
{
    expect_success(std::to_string(std::numeric_limits<int>::max()),
                   std::numeric_limits<int>::max() - 1);
}

void many_leading_zeroes_are_supported()
{
    expect_success(std::string(40, '0') + "1", 0);
}

void empty_input_error_is_propagated()
{
    expect_failure("", ParseError::empty_input);
}

void zero_is_still_out_of_range()
{
    expect_failure("0", ParseError::out_of_range);
    expect_failure("00000", ParseError::out_of_range);
}

void whitespace_is_not_normalized()
{
    expect_failure(" 42 ", ParseError::invalid_character);
    expect_failure("\t42", ParseError::invalid_character);
    expect_failure("42\n", ParseError::invalid_character);
}

void signs_are_not_accepted()
{
    expect_failure("+42", ParseError::invalid_character);
    expect_failure("-42", ParseError::invalid_character);
}

void nondigits_preserve_invalid_character()
{
    expect_failure("12x", ParseError::invalid_character);
    expect_failure("x12", ParseError::invalid_character);
    expect_failure("1.0", ParseError::invalid_character);
}

void overflow_error_is_propagated()
{
    expect_failure(std::string(64, '9'), ParseError::out_of_range);
}

void nondigit_overrides_overflow_error()
{
    expect_failure(std::string(64, '9') + "x", ParseError::invalid_character);
}

void embedded_null_is_invalid()
{
    constexpr char chars[] = {'4', '2', '\0', '3'};
    expect_failure(std::string_view{chars, sizeof(chars)}, ParseError::invalid_character);
}

} // namespace

cpp_mastery::test::TestSuite make_zero_based_index_suite()
{
    cpp_mastery::test::TestSuite suite{"zero-based-index"};
    suite.add("one maps to zero", one_maps_to_zero);
    suite.add("decimal maps to zero based", decimal_index_maps_to_zero_based);
    suite.add("leading zeroes", leading_zeroes_preserve_value);
    suite.add("INT_MAX boundary", maximum_integer_does_not_overflow);
    suite.add("many leading zeroes", many_leading_zeroes_are_supported);
    suite.add("propagates empty input", empty_input_error_is_propagated);
    suite.add("propagates zero out-of-range", zero_is_still_out_of_range);
    suite.add("rejects whitespace", whitespace_is_not_normalized);
    suite.add("rejects signs", signs_are_not_accepted);
    suite.add("rejects nondigits", nondigits_preserve_invalid_character);
    suite.add("propagates overflow", overflow_error_is_propagated);
    suite.add("invalid characters override overflow", nondigit_overrides_overflow_error);
    suite.add("rejects embedded null", embedded_null_is_invalid);
    return suite;
}
