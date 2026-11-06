#include <cpp_mastery/runtime/IdempotentActivationRuntime.hpp>

#include <stdexcept>

namespace cpp_mastery::runtime
{

IdempotentActivationRuntime::IdempotentActivationRuntime(const config::EngineConfig& initial,
                                                         ActivationBuilder& builder)
    : runtime_{initial, builder}
{
}

BackendGeneration IdempotentActivationRuntime::generation() const noexcept
{
    return runtime_.generation();
}

bool IdempotentActivationRuntime::device_lost() const noexcept
{
    return runtime_.device_lost();
}

bool IdempotentActivationRuntime::activation_pending() const noexcept
{
    return runtime_.activation_pending();
}

std::shared_ptr<const RuntimeSnapshot> IdempotentActivationRuntime::acquire() const noexcept
{
    return runtime_.acquire();
}

void IdempotentActivationRuntime::report_device_lost(const BackendGeneration generation)
{
    runtime_.report_device_lost(generation);
}

std::size_t IdempotentActivationRuntime::recorded_attempts() const noexcept
{
    return attempts_.size();
}

ActivationAttemptResult IdempotentActivationRuntime::result(const ActivationAttemptId id,
                                                            const Entry& entry) noexcept
{
    return {id, entry.state, entry.committed};
}

ActivationAttemptResult IdempotentActivationRuntime::recover_once(
    const ActivationAttemptId id, const BackendGeneration old_generation,
    const config::EngineConfig& next, ActivationBuilder& builder, SafeTeardownAuthority& authority)
{
    if (id.value == 0)
    {
        throw std::invalid_argument{"activation attempt ID must not be zero"};
    }

    const auto existing = attempts_.find(id.value);
    if (existing != attempts_.end())
    {
        const Entry& entry = existing->second;
        if (entry.original != old_generation ||
            entry.config.worker_threads != next.worker_threads ||
            entry.config.render_distance != next.render_distance)
        {
            throw std::invalid_argument{"activation attempt ID reused for different input"};
        }
        // GREEN: replay the recorded result, regardless of pending/terminal state.
        // No builder, authority, activation, or generation commit runs again.
        return result(id, entry);
    }
    if (pending_id_)
    {
        throw std::logic_error{"resolve the current activation before a new attempt"};
    }
    if (old_generation != runtime_.generation())
    {
        throw std::invalid_argument{"wrong recovery generation"};
    }
    if (!runtime_.device_lost())
    {
        throw std::logic_error{"activation recovery requires device loss"};
    }

    // Reserve dedup bookkeeping before entering the external activation path.
    auto [it, inserted] = attempts_.emplace(
        id.value, Entry{old_generation, next, ActivationAttemptState::pending, {}, std::nullopt});
    static_cast<void>(inserted);
    try
    {
        const auto committed = runtime_.recover(old_generation, next, builder, authority);
        if (!committed)
        {
            pending_id_ = id.value;
        }
        else
        {
            it->second.state = ActivationAttemptState::committed;
            it->second.committed = *committed;
        }
    }
    catch (...)
    {
        // An exception after external activation can still mean an UNKNOWN or
        // CONFIRMED STARTED attempt awaiting commit. Keep its identity pending.
        if (runtime_.activation_pending())
        {
            pending_id_ = id.value;
        }
        else
        {
            // No activation remains pending. This identity is terminal;
            // a genuine retry must use a new ID, never replay external effects.
            it->second.state = ActivationAttemptState::not_started;
        }
        throw;
    }
    return result(id, it->second);
}

ActivationAttemptResult IdempotentActivationRuntime::resolve(const ActivationAttemptId id,
                                                             const ActivationResolution resolution)
{
    if (resolution != ActivationResolution::confirmed_started &&
        resolution != ActivationResolution::confirmed_not_started)
    {
        throw std::invalid_argument{"unknown activation resolution"};
    }
    const auto found = attempts_.find(id.value);
    if (found == attempts_.end())
    {
        throw std::invalid_argument{"unknown activation attempt ID"};
    }
    Entry& entry = found->second;
    if (entry.state != ActivationAttemptState::pending)
    {
        const bool agrees = (entry.state == ActivationAttemptState::committed &&
                             resolution == ActivationResolution::confirmed_started) ||
                            (entry.state == ActivationAttemptState::not_started &&
                             resolution == ActivationResolution::confirmed_not_started);
        if (!agrees)
        {
            throw std::logic_error{"resolution contradicts recorded terminal result"};
        }
        return result(id, entry);
    }
    if (!pending_id_ || *pending_id_ != id.value)
    {
        throw std::logic_error{"attempt is not the pending activation"};
    }
    if (entry.resolution && *entry.resolution != resolution)
    {
        throw std::logic_error{"conflicting activation resolution"};
    }
    entry.resolution = resolution;
    // If committing throws, keep entry pending and retry the SAME confirmation
    // later. ActivationRecoveryRuntime does not call activate() on resolution.
    const auto committed = runtime_.resolve_pending(resolution);
    if (committed)
    {
        entry.state = ActivationAttemptState::committed;
        entry.committed = *committed;
    }
    else
    {
        entry.state = ActivationAttemptState::not_started;
    }
    pending_id_.reset();
    return result(id, entry);
}

} // namespace cpp_mastery::runtime
