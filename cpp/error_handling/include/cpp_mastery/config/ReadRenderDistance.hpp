#pragma once

#include <cpp_mastery/config/ConfigError.hpp>

#include <expected>
#include <string_view>

namespace cpp_mastery::config
{

// Accepts the strict decimal syntax of parse_positive_int and distances [1, 32].
// Uses the static field name "render_distance"; never borrows the input's storage.
[[nodiscard]] std::expected<int, ConfigError> read_render_distance(std::string_view input) noexcept;

} // namespace cpp_mastery::config
