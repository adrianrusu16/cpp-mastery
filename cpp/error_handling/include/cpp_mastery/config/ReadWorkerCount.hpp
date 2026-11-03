#pragma once

#include <cpp_mastery/parsing/ParsePositiveInt.hpp>

#include <expected>
#include <string_view>
#include <variant>

namespace cpp_mastery::config
{

enum class ConfigConstraintError
{
    worker_count_above_maximum,
};

// Either the underlying parsing error OR a configuration-specific validation error.
// A variant prevents impossible combinations of error kind and cause.
struct ConfigError
{
    // This API always uses the static literal "worker_threads", never a view into input.
    std::string_view field;
    std::variant<parsing::ParseError, ConfigConstraintError> cause;
};

// Accepts the strict decimal syntax of parse_positive_int and worker counts [1, 64].
// Errors retain the field name and either the original ParseError or a domain constraint.
[[nodiscard]] std::expected<int, ConfigError> read_worker_count(std::string_view input) noexcept;

} // namespace cpp_mastery::config
