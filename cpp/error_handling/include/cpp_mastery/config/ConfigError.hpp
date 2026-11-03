#pragma once

#include <cpp_mastery/parsing/ParsePositiveInt.hpp>

#include <string_view>
#include <variant>

namespace cpp_mastery::config
{

enum class ConfigConstraintError
{
    worker_count_above_maximum,
    render_distance_above_maximum,
};

// An error has one cause: either parsing or a configuration-specific constraint.
struct ConfigError
{
    // The validators use static field names. Never store a view into caller input here.
    std::string_view field;
    std::variant<parsing::ParseError, ConfigConstraintError> cause;
};

} // namespace cpp_mastery::config
