#include <cpp_mastery/config/ReadEngineConfig.hpp>
#include <cpp_mastery/config/ReadRenderDistance.hpp>
#include <cpp_mastery/config/ReadWorkerCount.hpp>

#include <utility>

namespace cpp_mastery::config
{

std::expected<EngineConfig, ConfigErrors> read_engine_config(const std::string_view workers,
                                                             const std::string_view distance)
{
    // Both validations are independent. Do not stop after the first failure.
    const auto worker_count = read_worker_count(workers);
    const auto render_distance = read_render_distance(distance);

    ConfigErrors errors;
    if (!worker_count)
    {
        errors.push_back(worker_count.error());
    }
    if (!render_distance)
    {
        errors.push_back(render_distance.error());
    }

    if (!errors.empty())
    {
        return std::unexpected(std::move(errors));
    }

    // Construct a complete configuration only after all fields passed validation.
    return EngineConfig{.worker_threads = *worker_count, .render_distance = *render_distance};
}

} // namespace cpp_mastery::config
