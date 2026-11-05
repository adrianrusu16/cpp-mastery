#include <cpp_mastery/runtime/DeviceRecoveryRuntime.hpp>

#include <limits>
#include <stdexcept>
#include <utility>

namespace cpp_mastery::runtime
{

DeviceRecoveryRuntime::DeviceRecoveryRuntime(const config::EngineConfig& initial,
                                             ResourceFactory& factory)
    : active_{std::make_unique<SubmissionRecoveryRuntime>(initial, factory)}
{
}

BackendGeneration DeviceRecoveryRuntime::generation() const noexcept
{
    return generation_;
}

bool DeviceRecoveryRuntime::device_lost() const noexcept
{
    return frozen_ || active_->device_lost();
}

std::shared_ptr<const RuntimeSnapshot> DeviceRecoveryRuntime::acquire() const noexcept
{
    return active_->acquire();
}

void DeviceRecoveryRuntime::require_generation(const BackendGeneration expected) const
{
    if (expected != generation_)
    {
        throw std::invalid_argument{"stale or wrong backend generation"};
    }
}

void DeviceRecoveryRuntime::require_running() const
{
    if (device_lost())
    {
        throw std::logic_error{"backend generation quarantined after device loss"};
    }
}

void DeviceRecoveryRuntime::publish(const BackendGeneration expected,
                                    const config::EngineConfig& next, ResourceFactory& factory)
{
    require_generation(expected);
    require_running();
    active_->publish(next, factory);
}

SubmissionResult DeviceRecoveryRuntime::try_submit(const BackendGeneration expected,
                                                   const GpuQueueKind queue,
                                                   SubmissionBackend& backend)
{
    require_generation(expected);
    require_running();
    auto result = active_->try_submit(active_->admit(), queue, backend);
    if (result.disposition == SubmissionDisposition::device_lost)
    {
        frozen_ = true;
    }
    return result;
}

std::uint64_t DeviceRecoveryRuntime::confirm_accepted(const BackendGeneration expected,
                                                      const std::uint64_t incident_id)
{
    require_generation(expected);
    require_running();
    return active_->confirm_accepted(incident_id);
}

void DeviceRecoveryRuntime::confirm_not_enqueued(const BackendGeneration expected,
                                                 const std::uint64_t incident_id)
{
    require_generation(expected);
    require_running();
    active_->confirm_not_enqueued(incident_id);
}

void DeviceRecoveryRuntime::complete_through(const BackendGeneration expected,
                                             const GpuQueueKind queue,
                                             const std::uint64_t submission)
{
    require_generation(expected);
    require_running();
    active_->complete_through(queue, submission);
}

std::size_t DeviceRecoveryRuntime::collect()
{
    return device_lost() ? 0 : active_->collect();
}

std::uint64_t DeviceRecoveryRuntime::submitted(const GpuQueueKind queue) const
{
    return active_->submitted(queue);
}

std::size_t DeviceRecoveryRuntime::unresolved_count() const noexcept
{
    return active_->unresolved_count();
}

std::size_t DeviceRecoveryRuntime::pending_admissions() const
{
    return active_->pending_admissions();
}

void DeviceRecoveryRuntime::report_device_lost(const BackendGeneration expected)
{
    require_generation(expected);
    frozen_ = true;
}

BackendGeneration DeviceRecoveryRuntime::recover(const BackendGeneration old_generation,
                                                 const config::EngineConfig& next,
                                                 ResourceFactory& new_factory,
                                                 SafeTeardownAuthority& authority)
{
    require_generation(old_generation);
    if (!device_lost())
    {
        throw std::logic_error{"cannot recover a running backend"};
    }
    if (generation_.value == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error{"backend generation exhausted"};
    }

    // Critical GREEN invariant: a new backend NEVER proves the old one is safe.
    if (!authority.confirmed_safe_to_destroy(old_generation))
    {
        throw std::logic_error{"safe teardown of old backend not confirmed"};
    }

    // Prepare first. If this throws, all old admissions, incidents and buffers
    // remain quarantined. No new timeline is published.
    auto staged = std::make_unique<SubmissionRecoveryRuntime>(next, new_factory);
    active_.swap(staged);
    generation_.value += 1;
    frozen_ = false;
    // staged now owns the old generation; its destruction drops old GPU owners
    // only AFTER the external authority confirmed that this is safe.
    return generation_;
}

} // namespace cpp_mastery::runtime
