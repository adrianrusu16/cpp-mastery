#include <cpp_mastery/config/ReadRenderDistance.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace
{

namespace config = cpp_mastery::config;
using config::ConfigConstraintError;
using config::ConfigError;
using config::read_render_distance;
using cpp_mastery::parsing::ParseError;

static_assert(std::is_same_v<decltype(read_render_distance("1")), std::expected<int, ConfigError>>);
static_assert(noexcept(read_render_distance("1")));

void expect_success(const std::string_view text, const int expected)
{
    const auto result = read_render_distance(text);
    REQUIRE(result.has_value());
    CHECK_EQ(*result, expected);
}

void expect_parse_failure(const std::string_view text, const ParseError cause)
{
    const auto result = read_render_distance(text);
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().field, "render_distance");
    const ParseError* const actual = std::get_if<ParseError>(&result.error().cause);
    REQUIRE(actual != nullptr);
    CHECK(*actual == cause);
}

void expect_domain_failure(const std::string_view text)
{
    const auto result = read_render_distance(text);
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().field, "render_distance");
    const auto* const actual = std::get_if<ConfigConstraintError>(&result.error().cause);
    REQUIRE(actual != nullptr);
    CHECK(*actual == ConfigConstraintError::render_distance_above_maximum);
}

void accepts_typical_distance()
{
    expect_success("24", 24);
}

void accepts_lower_bound()
{
    expect_success("1", 1);
}

void accepts_upper_bound()
{
    expect_success("32", 32);
}

void accepts_leading_zeroes()
{
    expect_success("00024", 24);
}

void accepts_long_zero_prefix()
{
    expect_success(std::string(80, '0') + "32", 32);
}

void reports_empty_input()
{
    expect_parse_failure("", ParseError::empty_input);
}

void reports_zero()
{
    expect_parse_failure("0", ParseError::out_of_range);
    expect_parse_failure("000", ParseError::out_of_range);
}

void rejects_whitespace()
{
    expect_parse_failure(" 24", ParseError::invalid_character);
    expect_parse_failure("24 ", ParseError::invalid_character);
    expect_parse_failure("	24", ParseError::invalid_character);
}

void rejects_signs()
{
    expect_parse_failure("+24", ParseError::invalid_character);
    expect_parse_failure("-24", ParseError::invalid_character);
}

void rejects_nondigits()
{
    expect_parse_failure("2x", ParseError::invalid_character);
    expect_parse_failure("2.4", ParseError::invalid_character);
    expect_parse_failure("1_0", ParseError::invalid_character);
}

void preserves_integer_overflow()
{
    expect_parse_failure(std::string(80, '9'), ParseError::out_of_range);
}

void invalid_character_precedes_overflow()
{
    expect_parse_failure(std::string(80, '9') + "x", ParseError::invalid_character);
}

void rejects_embedded_null()
{
    constexpr char data[] = {'2', '4', '\0', '9'};
    expect_parse_failure(std::string_view{data, sizeof(data)}, ParseError::invalid_character);
}

void rejects_distance_above_maximum()
{
    expect_domain_failure("33");
}

void preserves_domain_cause_for_int_max()
{
    expect_domain_failure(std::to_string(std::numeric_limits<int>::max()));
}

void leading_zeroes_do_not_hide_domain_failure()
{
    expect_domain_failure(std::string(80, '0') + "33");
}

void field_outlives_temporary_input()
{
    std::expected<int, ConfigError> result = 1;
    {
        const std::string temporary = std::string(100, '9') + "x";
        result = read_render_distance(temporary);
    }
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().field, "render_distance");
    const auto* const cause = std::get_if<ParseError>(&result.error().cause);
    REQUIRE(cause != nullptr);
    CHECK(*cause == ParseError::invalid_character);
}

} // namespace

cpp_mastery::test::TestSuite make_render_distance_suite()
{
    cpp_mastery::test::TestSuite suite{"render-distance"};
    suite.add("accepts typical distance", accepts_typical_distance);
    suite.add("accepts lower bound", accepts_lower_bound);
    suite.add("accepts upper bound", accepts_upper_bound);
    suite.add("accepts leading zeroes", accepts_leading_zeroes);
    suite.add("accepts long zero prefix", accepts_long_zero_prefix);
    suite.add("preserves empty-input cause", reports_empty_input);
    suite.add("preserves zero out-of-range", reports_zero);
    suite.add("rejects whitespace", rejects_whitespace);
    suite.add("rejects signs", rejects_signs);
    suite.add("rejects nondigits", rejects_nondigits);
    suite.add("preserves integer overflow", preserves_integer_overflow);
    suite.add("invalid character before overflow", invalid_character_precedes_overflow);
    suite.add("rejects embedded null", rejects_embedded_null);
    suite.add("rejects 33 as domain failure", rejects_distance_above_maximum);
    suite.add("INT_MAX is a domain failure", preserves_domain_cause_for_int_max);
    suite.add("leading zero domain failure", leading_zeroes_do_not_hide_domain_failure);
    suite.add("field outlives temporary input", field_outlives_temporary_input);
    return suite;
}
