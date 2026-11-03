#include <cpp_mastery/config/ReadWorkerCount.hpp>
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
namespace parsing = cpp_mastery::parsing;

using config::ConfigConstraintError;
using config::ConfigError;
using config::read_worker_count;
using parsing::ParseError;

static_assert(std::is_same_v<decltype(read_worker_count("1")), std::expected<int, ConfigError>>);
static_assert(noexcept(read_worker_count("1")));

void expect_success(const std::string_view input, const int expected)
{
    const auto result = read_worker_count(input);
    REQUIRE(result.has_value());
    CHECK_EQ(*result, expected);
}

void expect_parse_failure(const std::string_view input, const ParseError expected)
{
    const auto result = read_worker_count(input);
    REQUIRE_FALSE(result.has_value());
    const ConfigError& error = result.error();
    CHECK(error.field == "worker_threads");
    const ParseError* const cause = std::get_if<ParseError>(&error.cause);
    REQUIRE(cause != nullptr);
    CHECK(*cause == expected);
}

void expect_domain_failure(const std::string_view input)
{
    const auto result = read_worker_count(input);
    REQUIRE_FALSE(result.has_value());
    const ConfigError& error = result.error();
    CHECK(error.field == "worker_threads");
    const ConfigConstraintError* const cause = std::get_if<ConfigConstraintError>(&error.cause);
    REQUIRE(cause != nullptr);
    CHECK(*cause == ConfigConstraintError::worker_count_above_maximum);
}

void accepts_typical_worker_count()
{
    expect_success("12", 12);
}

void accepts_lower_bound()
{
    expect_success("1", 1);
}

void accepts_upper_bound()
{
    expect_success("64", 64);
}

void preserves_leading_zeros()
{
    expect_success("00012", 12);
}

void accepts_many_leading_zeros()
{
    expect_success(std::string(80, '0') + "64", 64);
}

void translates_empty_input()
{
    expect_parse_failure("", ParseError::empty_input);
}

void translates_zero_without_losing_cause()
{
    expect_parse_failure("0", ParseError::out_of_range);
    expect_parse_failure("00000", ParseError::out_of_range);
}

void rejects_whitespace_without_trimming()
{
    expect_parse_failure(" 12", ParseError::invalid_character);
    expect_parse_failure("12 ", ParseError::invalid_character);
    expect_parse_failure("\t12", ParseError::invalid_character);
}

void rejects_signs()
{
    expect_parse_failure("+12", ParseError::invalid_character);
    expect_parse_failure("-12", ParseError::invalid_character);
}

void translates_invalid_characters()
{
    expect_parse_failure("12x", ParseError::invalid_character);
    expect_parse_failure("x12", ParseError::invalid_character);
    expect_parse_failure("1.2", ParseError::invalid_character);
}

void preserves_numeric_overflow_error()
{
    expect_parse_failure(std::string(80, '9'), ParseError::out_of_range);
}

void invalid_character_precedes_overflow()
{
    expect_parse_failure(std::string(80, '9') + "x", ParseError::invalid_character);
}

void embedded_null_is_not_ignored()
{
    constexpr char input[] = {'1', '2', '\0', '3'};
    expect_parse_failure(std::string_view{input, sizeof(input)}, ParseError::invalid_character);
}

void rejects_value_above_domain_maximum()
{
    expect_domain_failure("65");
}

void rejects_int_max_as_domain_error()
{
    expect_domain_failure(std::to_string(std::numeric_limits<int>::max()));
}

void domain_error_is_not_hidden_by_leading_zeros()
{
    expect_domain_failure(std::string(80, '0') + "65");
}

void error_field_survives_discarded_input()
{
    // The ConfigError must own no view into the temporary input's storage.
    std::expected<int, ConfigError> result = 1;
    {
        const std::string temporary = std::string(256, '9') + "x";
        result = read_worker_count(temporary);
    }

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().field == "worker_threads");
    const ParseError* const cause = std::get_if<ParseError>(&result.error().cause);
    REQUIRE(cause != nullptr);
    CHECK(*cause == ParseError::invalid_character);
}

} // namespace

cpp_mastery::test::TestSuite make_worker_count_suite()
{
    cpp_mastery::test::TestSuite suite{"worker-count"};
    suite.add("accepts typical count", accepts_typical_worker_count);
    suite.add("accepts lower bound", accepts_lower_bound);
    suite.add("accepts upper bound", accepts_upper_bound);
    suite.add("preserves leading zeros", preserves_leading_zeros);
    suite.add("accepts many leading zeros", accepts_many_leading_zeros);
    suite.add("preserves empty-input cause", translates_empty_input);
    suite.add("preserves zero cause", translates_zero_without_losing_cause);
    suite.add("rejects whitespace", rejects_whitespace_without_trimming);
    suite.add("rejects signs", rejects_signs);
    suite.add("preserves malformed-input cause", translates_invalid_characters);
    suite.add("preserves integer-overflow cause", preserves_numeric_overflow_error);
    suite.add("invalid character precedes overflow", invalid_character_precedes_overflow);
    suite.add("rejects embedded null", embedded_null_is_not_ignored);
    suite.add("rejects 65 as domain error", rejects_value_above_domain_maximum);
    suite.add("rejects INT_MAX as domain error", rejects_int_max_as_domain_error);
    suite.add("rejects leading-zero domain overflow", domain_error_is_not_hidden_by_leading_zeros);
    suite.add("error field outlives input", error_field_survives_discarded_input);
    return suite;
}
