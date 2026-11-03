#include <cpp_mastery/parsing/ParseZeroBasedIndex.hpp>

namespace cpp_mastery::parsing
{

std::expected<int, ParseError> parse_zero_based_index(const std::string_view input) noexcept
{
    const auto parsed = parse_positive_int(input);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }

    // parse_positive_int guarantees [1, INT_MAX], so subtraction cannot underflow.
    return *parsed - 1;
}

} // namespace cpp_mastery::parsing
