#pragma once

#include <cpp_mastery/parsing/ParsePositiveInt.hpp>

#include <expected>
#include <string_view>

namespace cpp_mastery::parsing
{

// Converts a one-based ASCII decimal index in [1, INT_MAX] to [0, INT_MAX - 1].
// Propagates ParseError from parse_positive_int without reclassification.
[[nodiscard]] std::expected<int, ParseError>
parse_zero_based_index(std::string_view input) noexcept;

} // namespace cpp_mastery::parsing
