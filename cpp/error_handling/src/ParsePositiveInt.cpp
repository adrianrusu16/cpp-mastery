#include <cpp_mastery/parsing/ParsePositiveInt.hpp>

#include <charconv>
#include <system_error>

namespace cpp_mastery::parsing
{

std::expected<int, ParseError> parse_positive_int(const std::string_view input) noexcept
{
    if (input.empty())
    {
        return std::unexpected(ParseError::empty_input);
    }

    // Keep the contract strict, including cases where an earlier prefix overflows.
    for (const char character : input)
    {
        if (character < '0' || character > '9')
        {
            return std::unexpected(ParseError::invalid_character);
        }
    }

    int value = 0;
    const char* const end = input.data() + input.size();
    const auto [next, error] = std::from_chars(input.data(), end, value, 10);

    if (error == std::errc::result_out_of_range)
    {
        return std::unexpected(ParseError::out_of_range);
    }
    if (error != std::errc{} || next != end)
    {
        return std::unexpected(ParseError::invalid_character);
    }
    if (value == 0)
    {
        return std::unexpected(ParseError::out_of_range);
    }

    return value;
}

} // namespace cpp_mastery::parsing
