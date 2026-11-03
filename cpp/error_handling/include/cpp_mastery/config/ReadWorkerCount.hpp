#pragma once

#include <cpp_mastery/config/ConfigError.hpp>

#include <expected>
#include <string_view>

namespace cpp_mastery::config
{

// Accepts the strict decimal syntax of parse_positive_int and worker counts [1, 64].
// Errors retain the field name and either the original ParseError or a domain constraint.
[[nodiscard]] std::expected<int, ConfigError> read_worker_count(std::string_view input) noexcept;

} // namespace cpp_mastery::config
