#include <cpp_mastery/runtime/SnapshotRuntime.hpp>

#include <stdexcept>
#include <utility>

namespace cpp_mastery::runtime
{

RuntimeSnapshot::RuntimeSnapshot(const config::EngineConfig& config, ResourceFactory& factory)
    : settings_{config.worker_threads, config.render_distance}
{
    auto geometry = factory.allocate(BufferKind::geometry, config.render_distance);
    if (!geometry)
    {
        throw std::runtime_error{"snapshot factory returned null geometry"};
    }
    geometry_ = std::move(geometry);

    auto lighting = factory.allocate(BufferKind::lighting, config.render_distance);
    if (!lighting)
    {
        throw std::runtime_error{"snapshot factory returned null lighting"};
    }
    lighting_ = std::move(lighting);
}

SnapshotRuntime::SnapshotRuntime(const config::EngineConfig& initial, ResourceFactory& factory)
    : current_{std::make_shared<const RuntimeSnapshot>(initial, factory)}
{
}

std::shared_ptr<const RuntimeSnapshot> SnapshotRuntime::acquire() const noexcept
{
    return current_.load(std::memory_order_acquire);
}

void SnapshotRuntime::publish(const config::EngineConfig& next, ResourceFactory& factory)
{
    // Prepare all fallible work without changing what concurrent readers see.
    auto staged = std::make_shared<const RuntimeSnapshot>(next, factory);
    // The published pointer changes as one atomic operation. Readers keep their
    // old shared ownership, so retired buffers outlive their last reader.
    current_.store(std::move(staged), std::memory_order_release);
}

} // namespace cpp_mastery::runtime
