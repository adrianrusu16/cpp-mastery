#pragma once

#include <cpp_mastery/runtime/RuntimeState.hpp>

#include <atomic>
#include <memory>

namespace cpp_mastery::runtime
{

// Immutable after construction. Every snapshot owns its render buffers.
// Acquiring a shared_ptr keeps both buffers alive even after a new publication.
class RuntimeSnapshot final
{
  public:
    RuntimeSnapshot(const config::EngineConfig& config, ResourceFactory& factory);

    [[nodiscard]] const RuntimeSettings& settings() const noexcept
    {
        return settings_;
    }
    [[nodiscard]] const RenderBuffer* geometry_buffer() const noexcept
    {
        return geometry_.get();
    }
    [[nodiscard]] const RenderBuffer* lighting_buffer() const noexcept
    {
        return lighting_.get();
    }

  private:
    const RuntimeSettings settings_;
    std::unique_ptr<const RenderBuffer> geometry_;
    std::unique_ptr<const RenderBuffer> lighting_;
};

// Single-writer publication, concurrent readers. Does not control worker pools.
// Do not mutate the factory concurrently while publication is in progress.
class SnapshotRuntime final
{
  public:
    SnapshotRuntime(const config::EngineConfig& initial, ResourceFactory& factory);

    [[nodiscard]] std::shared_ptr<const RuntimeSnapshot> acquire() const noexcept;

    // Strong guarantee: a failed preparation leaves the published snapshot intact.
    // May allocate and throw. Concurrent publication by multiple writers is outside
    // this teaching model's contract.
    void publish(const config::EngineConfig& next, ResourceFactory& factory);

  private:
    std::atomic<std::shared_ptr<const RuntimeSnapshot>> current_;
};

} // namespace cpp_mastery::runtime
