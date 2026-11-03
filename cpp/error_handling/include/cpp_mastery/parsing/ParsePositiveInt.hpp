#pragma once

#include <expected>
#include <string_view>

namespace cpp_mastery::parsing
{

enum class ParseError
{
    empty_input,
    invalid_character,
    out_of_range,
};

// Accepts only ASCII decimal digits and values in [1, INT_MAX].
// No whitespace, signs, locale handling, or implicit normalization.
[[nodiscard]] std::expected<int, ParseError> parse_positive_int(std::string_view input) noexcept;

} // namespace cpp_mastery::parsing
