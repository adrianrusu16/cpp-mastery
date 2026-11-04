#include <cpp_mastery/runtime/CoordinatedRuntime.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cpp_mastery::runtime
{

namespace
{

[[nodiscard]] std::size_t queue_index(const GpuQueueKind queue)
{
    switch (queue)
    {
    case GpuQueueKind::graphics:
        return 0;
    case GpuQueueKind::compute:
        return 1;
    case GpuQueueKind::transfer:
        return 2;
    }
    throw std::invalid_argument{"unknown GPU queue"};
}

} // namespace

struct CoordinatedRuntime::Generation
{
    explicit Generation(std::shared_ptr<const RuntimeSnapshot> value) : snapshot{std::move(value)}
    {
    }

    std::shared_ptr<const RuntimeSnapshot> snapshot;
    std::array<std::uint64_t, 3> last_use{};
    std::size_t pending = 0;
    bool sealed = false;
};

struct CoordinatedRuntime::State
{
    explicit State(std::shared_ptr<const RuntimeSnapshot> initial)
        : active{std::make_shared<Generation>(std::move(initial))}, current{active->snapshot}
    {
    }

    mutable std::mutex mutex;
    std::shared_ptr<Generation> active;
    std::atomic<std::shared_ptr<const RuntimeSnapshot>> current;
    std::vector<std::shared_ptr<Generation>> retired;
    MultiQueueGpuRetirementQueue gpu;
};

CoordinatedRuntime::Admission::Admission(std::shared_ptr<State> state,
                                         std::shared_ptr<Generation> generation) noexcept
    : state_{std::move(state)}, generation_{std::move(generation)}
{
}

CoordinatedRuntime::Admission& CoordinatedRuntime::Admission::operator=(Admission&& other) noexcept
{
    if (this != &other)
    {
        cancel();
        state_ = std::move(other.state_);
        generation_ = std::move(other.generation_);
    }
    return *this;
}

CoordinatedRuntime::Admission::~Admission()
{
    cancel();
}

std::shared_ptr<const RuntimeSnapshot> CoordinatedRuntime::Admission::snapshot() const noexcept
{
    return generation_ ? generation_->snapshot : nullptr;
}

std::uint64_t CoordinatedRuntime::Admission::submit(const GpuQueueKind queue)
{
    const auto index = queue_index(queue);
    if (!state_ || !generation_)
    {
        throw std::logic_error{"admission already consumed or cancelled"};
    }

    auto state = state_;
    auto generation = generation_;
    std::lock_guard lock{state->mutex};
    // Submission acceptance is serialized with publication and retirement.
    // Registration failure does not consume the reservation.
    const auto serial = state->gpu.submit(queue, generation->snapshot);
    generation->last_use[index] = serial;
    --generation->pending;
    generation_.reset();
    state_.reset();
    return serial;
}

std::uint64_t CoordinatedRuntime::Admission::submit_graphics_after(const CrossQueueHandoff& handoff)
{
    if (!handoff.graphics_read_ready())
    {
        throw std::logic_error{"graphics read missing cross-queue synchronization"};
    }
    return submit(GpuQueueKind::graphics);
}

void CoordinatedRuntime::Admission::cancel() noexcept
{
    auto state = std::move(state_);
    auto generation = std::move(generation_);
    if (state)
    {
        std::lock_guard lock{state->mutex};
        --generation->pending;
    }
}

CoordinatedRuntime::CoordinatedRuntime(const config::EngineConfig& initial,
                                       ResourceFactory& factory)
    : state_{std::make_shared<State>(std::make_shared<const RuntimeSnapshot>(initial, factory))}
{
}

std::shared_ptr<const RuntimeSnapshot> CoordinatedRuntime::acquire() const noexcept
{
    return state_->current.load(std::memory_order_acquire);
}

CoordinatedRuntime::Admission CoordinatedRuntime::admit()
{
    std::lock_guard lock{state_->mutex};
    // No allocations here: existing shared_ptr control blocks are retained.
    auto generation = state_->active;
    ++generation->pending;
    return Admission{state_, std::move(generation)};
}

void CoordinatedRuntime::publish(const config::EngineConfig& next, ResourceFactory& factory)
{
    // All fallible resource creation precedes the publication lock.
    auto next_generation =
        std::make_shared<Generation>(std::make_shared<const RuntimeSnapshot>(next, factory));

    std::lock_guard lock{state_->mutex};
    // Reserve old-generation ownership before modifying published state.
    // If vector growth throws, the old snapshot remains published.
    state_->retired.push_back(state_->active);
    state_->active = std::move(next_generation);
    state_->current.store(state_->active->snapshot, std::memory_order_release);
}

void CoordinatedRuntime::complete_through(const GpuQueueKind queue, const std::uint64_t submission)
{
    std::lock_guard lock{state_->mutex};
    state_->gpu.complete_through(queue, submission);
}

std::size_t CoordinatedRuntime::collect()
{
    std::lock_guard lock{state_->mutex};
    for (const auto& generation : state_->retired)
    {
        if (!generation->sealed && generation->pending == 0)
        {
            // A failed allocation in retire() leaves the generation here for
            // retry, keeping all its resources alive.
            state_->gpu.retire(generation->snapshot);
            generation->sealed = true;
        }
    }

    const std::size_t collected = state_->gpu.collect();
    std::erase_if(state_->retired,
                  [this](const std::shared_ptr<Generation>& generation)
                  {
                      if (!generation->sealed)
                      {
                          return false;
                      }
                      constexpr std::array<GpuQueueKind, 3> queues = {
                          GpuQueueKind::graphics, GpuQueueKind::compute, GpuQueueKind::transfer};
                      for (std::size_t index = 0; index < queues.size(); ++index)
                      {
                          if (generation->last_use[index] > state_->gpu.completed(queues[index]))
                          {
                              return false;
                          }
                      }
                      return true;
                  });
    return collected;
}

std::uint64_t CoordinatedRuntime::submitted(const GpuQueueKind queue) const
{
    std::lock_guard lock{state_->mutex};
    return state_->gpu.submitted(queue);
}

std::size_t CoordinatedRuntime::pending_admissions() const
{
    std::lock_guard lock{state_->mutex};
    std::size_t total = state_->active->pending;
    for (const auto& generation : state_->retired)
    {
        total += generation->pending;
    }
    return total;
}

std::size_t CoordinatedRuntime::retired_generations() const
{
    std::lock_guard lock{state_->mutex};
    return state_->retired.size();
}

} // namespace cpp_mastery::runtime
