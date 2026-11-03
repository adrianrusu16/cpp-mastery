#include <cpp_mastery/config/ReadWorkerCount.hpp>

namespace cpp_mastery::config
{

std::expected<int, ConfigError> read_worker_count(const std::string_view input) noexcept
{
    constexpr std::string_view field = "worker_threads";

    const auto parsed = parsing::parse_positive_int(input);
    if (!parsed)
    {
        // Preserve the underlying parsing cause while attaching domain context.
        return std::unexpected(ConfigError{field, parsed.error()});
    }

    if (*parsed > 64)
    {
        // 65 and INT_MAX are valid integers but invalid worker counts.
        return std::unexpected(
            ConfigError{field, ConfigConstraintError::worker_count_above_maximum});
    }

    return *parsed;
}

} // namespace cpp_mastery::config
