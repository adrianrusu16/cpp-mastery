#include <cpp_mastery/parsing/ParsePositiveInt.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <limits>
#include <string>
#include <string_view>

namespace
{

using cpp_mastery::parsing::parse_positive_int;
using cpp_mastery::parsing::ParseError;

void expect_success(const std::string_view text, const int expected)
{
    const auto result = parse_positive_int(text);
    REQUIRE(result.has_value());
    CHECK_EQ(*result, expected);
}

void expect_failure(const std::string_view text, const ParseError expected)
{
    const auto result = parse_positive_int(text);
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error(), expected);
}

void parses_decimal_integer()
{
    expect_success("42", 42);
}

void parses_one()
{
    expect_success("1", 1);
}

void permits_leading_zeros()
{
    expect_success("0042", 42);
}

void parses_int_max()
{
    const int maximum = std::numeric_limits<int>::max();
    expect_success(std::to_string(maximum), maximum);
}

void permits_many_leading_zeros()
{
    const int maximum = std::numeric_limits<int>::max();
    expect_success(std::string(40, '0') + std::to_string(maximum), maximum);
}

void rejects_empty_input()
{
    expect_failure("", ParseError::empty_input);
}

void rejects_zero()
{
    expect_failure("0", ParseError::out_of_range);
    expect_failure("000", ParseError::out_of_range);
}

void rejects_whitespace()
{
    expect_failure(" 42", ParseError::invalid_character);
    expect_failure("42 ", ParseError::invalid_character);
    expect_failure("\t42", ParseError::invalid_character);
    expect_failure("42\n", ParseError::invalid_character);
}

void rejects_signs()
{
    expect_failure("+42", ParseError::invalid_character);
    expect_failure("-42", ParseError::invalid_character);
}

void rejects_other_characters()
{
    expect_failure("12x", ParseError::invalid_character);
    expect_failure("x12", ParseError::invalid_character);
    expect_failure("12.0", ParseError::invalid_character);
    expect_failure("0x10", ParseError::invalid_character);
    expect_failure("1_000", ParseError::invalid_character);
}

void rejects_integer_overflow()
{
    expect_failure(std::to_string(std::numeric_limits<int>::max()) + "0", ParseError::out_of_range);
    expect_failure(std::string(64, '9'), ParseError::out_of_range);
}

void classifies_invalid_character_after_large_prefix()
{
    // Invalid characters take precedence over overflow by contract.
    expect_failure(std::string(64, '9') + "x", ParseError::invalid_character);
}

void rejects_embedded_null()
{
    constexpr char characters[] = {'4', '2', '\0', '7'};
    expect_failure(std::string_view{characters, sizeof(characters)}, ParseError::invalid_character);
}

} // namespace

cpp_mastery::test::TestSuite make_positive_int_suite()
{
    cpp_mastery::test::TestSuite suite{"positive-int"};
    suite.add("parses decimal integer", parses_decimal_integer);
    suite.add("parses one", parses_one);
    suite.add("permits leading zeros", permits_leading_zeros);
    suite.add("parses INT_MAX", parses_int_max);
    suite.add("permits many leading zeros", permits_many_leading_zeros);
    suite.add("rejects empty input", rejects_empty_input);
    suite.add("rejects zero", rejects_zero);
    suite.add("rejects whitespace", rejects_whitespace);
    suite.add("rejects signs", rejects_signs);
    suite.add("rejects other characters", rejects_other_characters);
    suite.add("rejects integer overflow", rejects_integer_overflow);
    suite.add("invalid character after overflow", classifies_invalid_character_after_large_prefix);
    suite.add("rejects embedded null", rejects_embedded_null);
    return suite;
}
