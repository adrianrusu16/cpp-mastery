#pragma once

#include <cpp_mastery/runtime/ParkedWorkerPool.hpp>
#include <cpp_mastery/runtime/RuntimeState.hpp>

#include <memory>

namespace cpp_mastery::runtime
{

// Single-controller teaching model. Do not read or modify this object
// concurrently; the commit is exception-atomic, not a concurrent publication API.
class WorkerRuntime final
{
  public:
    WorkerRuntime(const config::EngineConfig& initial, ResourceFactory& factory,
                  std::shared_ptr<WorkerStats> stats,
                  ParkedWorkerPool::BeforeLaunch before_launch = {});

    // Strong exception guarantee for this model: no staged worker executes a
    // task, and active settings/resources/pool are unchanged if preparation fails.
    void apply(const config::EngineConfig& next, ResourceFactory& factory,
               std::shared_ptr<WorkerStats> stats,
               ParkedWorkerPool::BeforeLaunch before_launch = {});

    [[nodiscard]] const RuntimeState& state() const noexcept
    {
        return *active_->state;
    }
    [[nodiscard]] int active_workers() const noexcept
    {
        return active_->pool->size();
    }

  private:
    struct Bundle
    {
        // Destruction order: pool first (stop/join workers), state second.
        std::unique_ptr<RuntimeState> state;
        std::unique_ptr<ParkedWorkerPool> pool;
    };

    [[nodiscard]] static std::unique_ptr<Bundle>
    prepare(const config::EngineConfig& next, ResourceFactory& factory,
            std::shared_ptr<WorkerStats> stats, ParkedWorkerPool::BeforeLaunch before_launch);

    std::unique_ptr<Bundle> active_;
};

} // namespace cpp_mastery::runtime
