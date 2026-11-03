#include <cpp_mastery/runtime/WorkerRuntime.hpp>

#include <utility>

namespace cpp_mastery::runtime
{

std::unique_ptr<WorkerRuntime::Bundle>
WorkerRuntime::prepare(const config::EngineConfig& next, ResourceFactory& factory,
                       std::shared_ptr<WorkerStats> stats,
                       ParkedWorkerPool::BeforeLaunch before_launch)
{
    auto staged_state = std::make_unique<RuntimeState>(next, factory);
    auto staged_pool = std::make_unique<ParkedWorkerPool>(next.worker_threads, std::move(stats),
                                                          std::move(before_launch));
    return std::make_unique<Bundle>(std::move(staged_state), std::move(staged_pool));
}

WorkerRuntime::WorkerRuntime(const config::EngineConfig& initial, ResourceFactory& factory,
                             std::shared_ptr<WorkerStats> stats,
                             ParkedWorkerPool::BeforeLaunch before_launch)
    : active_{prepare(initial, factory, std::move(stats), std::move(before_launch))}
{
    active_->pool->activate();
}

void WorkerRuntime::apply(const config::EngineConfig& next, ResourceFactory& factory,
                          std::shared_ptr<WorkerStats> stats,
                          ParkedWorkerPool::BeforeLaunch before_launch)
{
    // All fallible resource and thread creation occurs before publication.
    auto staged = prepare(next, factory, std::move(stats), std::move(before_launch));

    // Publish resources, settings and the parked pool as one bundle. The swap
    // is noexcept; activation uses only an atomic store and notification.
    active_.swap(staged);
    active_->pool->activate();
    // staged destructs the old bundle: join old workers before freeing resources.
}

} // namespace cpp_mastery::runtime
