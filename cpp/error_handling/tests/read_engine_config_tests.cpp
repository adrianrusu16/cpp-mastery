#include <cpp_mastery/config/ReadEngineConfig.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstddef>
#include <expected>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{

namespace config = cpp_mastery::config;
using config::ConfigConstraintError;
using config::ConfigError;
using config::ConfigErrors;
using config::EngineConfig;
using config::read_engine_config;
using cpp_mastery::parsing::ParseError;

static_assert(std::is_same_v<decltype(read_engine_config("1", "1")),
                             std::expected<EngineConfig, ConfigErrors>>);
static_assert(!noexcept(read_engine_config("1", "1")));

struct ExpectedError
{
    std::string_view field;
    std::variant<ParseError, ConfigConstraintError> cause;
};

void expect_success(const std::string_view workers, const std::string_view distance,
                    const int expected_workers, const int expected_distance)
{
    const auto result = read_engine_config(workers, distance);
    REQUIRE(result.has_value());
    CHECK_EQ(result->worker_threads, expected_workers);
    CHECK_EQ(result->render_distance, expected_distance);
}

void expect_errors(const std::string_view workers, const std::string_view distance,
                   const std::initializer_list<ExpectedError> expected)
{
    const auto result = read_engine_config(workers, distance);
    REQUIRE_FALSE(result.has_value());
    const ConfigErrors& errors = result.error();
    REQUIRE_EQ(errors.size(), expected.size());

    std::size_t index = 0;
    for (const ExpectedError& item : expected)
    {
        CHECK_EQ(errors[index].field, item.field);
        CHECK(errors[index].cause == item.cause);
        ++index;
    }
}

void accepts_valid_configuration()
{
    expect_success("8", "24", 8, 24);
}

void accepts_lower_boundaries()
{
    expect_success("1", "1", 1, 1);
}

void accepts_upper_boundaries()
{
    expect_success("64", "32", 64, 32);
}

void preserves_leading_zeroes()
{
    expect_success("0008", "00024", 8, 24);
}

void accepts_long_zero_prefixes()
{
    expect_success(std::string(80, '0') + "64", std::string(80, '0') + "32", 64, 32);
}

void worker_parse_failure_preserves_cause()
{
    expect_errors("12x", "24", {{"worker_threads", ParseError::invalid_character}});
}

void worker_domain_failure_preserves_cause()
{
    expect_errors("65", "24",
                  {{"worker_threads", ConfigConstraintError::worker_count_above_maximum}});
}

void distance_parse_failure_preserves_cause()
{
    expect_errors("8", "24x", {{"render_distance", ParseError::invalid_character}});
}

void distance_domain_failure_preserves_cause()
{
    expect_errors("8", "33",
                  {{"render_distance", ConfigConstraintError::render_distance_above_maximum}});
}

void returns_both_parse_failures_in_field_order()
{
    expect_errors("12x", " 24",
                  {{"worker_threads", ParseError::invalid_character},
                   {"render_distance", ParseError::invalid_character}});
}

void returns_both_domain_failures_in_field_order()
{
    expect_errors("65", "33",
                  {{"worker_threads", ConfigConstraintError::worker_count_above_maximum},
                   {"render_distance", ConfigConstraintError::render_distance_above_maximum}});
}

void returns_domain_then_parse_failure()
{
    expect_errors("65", "12x",
                  {{"worker_threads", ConfigConstraintError::worker_count_above_maximum},
                   {"render_distance", ParseError::invalid_character}});
}

void returns_parse_then_domain_failure()
{
    expect_errors("12x", "33",
                  {{"worker_threads", ParseError::invalid_character},
                   {"render_distance", ConfigConstraintError::render_distance_above_maximum}});
}

void returns_two_empty_input_errors()
{
    expect_errors("", "",
                  {{"worker_threads", ParseError::empty_input},
                   {"render_distance", ParseError::empty_input}});
}

void returns_zero_and_distance_limit_errors()
{
    expect_errors("0", "33",
                  {{"worker_threads", ParseError::out_of_range},
                   {"render_distance", ConfigConstraintError::render_distance_above_maximum}});
}

void parsing_overflow_is_not_a_domain_error()
{
    expect_errors(std::string(80, '9'), "33",
                  {{"worker_threads", ParseError::out_of_range},
                   {"render_distance", ConfigConstraintError::render_distance_above_maximum}});
}

void invalid_character_precedes_overflow_in_each_field()
{
    const std::string bad = std::string(80, '9') + "x";
    expect_errors(bad, bad,
                  {{"worker_threads", ParseError::invalid_character},
                   {"render_distance", ParseError::invalid_character}});
}

void errors_do_not_borrow_input_storage()
{
    std::expected<EngineConfig, ConfigErrors> result = EngineConfig{1, 1};
    {
        const std::string workers = std::string(100, '9') + "x";
        const std::string distance = std::string(100, '9') + "x";
        result = read_engine_config(workers, distance);
    }

    REQUIRE_FALSE(result.has_value());
    REQUIRE_EQ(result.error().size(), 2U);
    CHECK_EQ(result.error()[0].field, "worker_threads");
    CHECK_EQ(result.error()[1].field, "render_distance");
    CHECK(std::holds_alternative<ParseError>(result.error()[0].cause));
    CHECK(std::holds_alternative<ParseError>(result.error()[1].cause));
}

void valid_input_does_not_retain_old_errors()
{
    auto result = read_engine_config("65", "33");
    REQUIRE_FALSE(result.has_value());
    result = read_engine_config("8", "24");
    REQUIRE(result.has_value());
    CHECK_EQ(result->worker_threads, 8);
    CHECK_EQ(result->render_distance, 24);
}

} // namespace

cpp_mastery::test::TestSuite make_engine_config_suite()
{
    cpp_mastery::test::TestSuite suite{"engine-config"};
    suite.add("accepts valid configuration", accepts_valid_configuration);
    suite.add("accepts lower boundaries", accepts_lower_boundaries);
    suite.add("accepts upper boundaries", accepts_upper_boundaries);
    suite.add("preserves leading zeroes", preserves_leading_zeroes);
    suite.add("accepts long zero prefixes", accepts_long_zero_prefixes);
    suite.add("preserves worker parse cause", worker_parse_failure_preserves_cause);
    suite.add("preserves worker domain cause", worker_domain_failure_preserves_cause);
    suite.add("preserves distance parse cause", distance_parse_failure_preserves_cause);
    suite.add("preserves distance domain cause", distance_domain_failure_preserves_cause);
    suite.add("collects two parse errors in order", returns_both_parse_failures_in_field_order);
    suite.add("collects two domain errors in order", returns_both_domain_failures_in_field_order);
    suite.add("collects domain then parse", returns_domain_then_parse_failure);
    suite.add("collects parse then domain", returns_parse_then_domain_failure);
    suite.add("collects two empty inputs", returns_two_empty_input_errors);
    suite.add("collects zero and domain failure", returns_zero_and_distance_limit_errors);
    suite.add("keeps overflow as parse cause", parsing_overflow_is_not_a_domain_error);
    suite.add("invalid character beats overflow",
              invalid_character_precedes_overflow_in_each_field);
    suite.add("errors outlive inputs", errors_do_not_borrow_input_storage);
    suite.add("success replaces previous errors", valid_input_does_not_retain_old_errors);
    return suite;
}
