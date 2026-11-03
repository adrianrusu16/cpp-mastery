#include <cpp_mastery/config/ReadRenderDistance.hpp>

namespace cpp_mastery::config
{

std::expected<int, ConfigError> read_render_distance(const std::string_view input) noexcept
{
    constexpr std::string_view field = "render_distance";

    const auto parsed = parsing::parse_positive_int(input);
    if (!parsed)
    {
        return std::unexpected(ConfigError{field, parsed.error()});
    }

    if (*parsed > 32)
    {
        return std::unexpected(
            ConfigError{field, ConfigConstraintError::render_distance_above_maximum});
    }

    return *parsed;
}

} // namespace cpp_mastery::config
