#pragma once

#include <cpp_mastery/config/ConfigError.hpp>

#include <expected>
#include <string_view>
#include <vector>

namespace cpp_mastery::config
{

struct EngineConfig
{
    int worker_threads;
    int render_distance;
};

using ConfigErrors = std::vector<ConfigError>;

// Validate both independent fields; report errors in worker-then-distance order.
// Return EngineConfig only when both values satisfy their domain contracts.
// Deliberately not noexcept: collecting errors in std::vector may allocate.
[[nodiscard]] std::expected<EngineConfig, ConfigErrors>
read_engine_config(std::string_view workers, std::string_view distance);

} // namespace cpp_mastery::config
