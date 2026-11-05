#include <cpp_mastery/runtime/SubmissionRecovery.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cpp_mastery::runtime
{

SubmissionRecoveryRuntime::SubmissionRecoveryRuntime(const config::EngineConfig& initial,
                                                     ResourceFactory& factory)
    : runtime_{initial, factory}
{
}

void SubmissionRecoveryRuntime::validate_queue(const GpuQueueKind queue)
{
    switch (queue)
    {
    case GpuQueueKind::graphics:
    case GpuQueueKind::compute:
    case GpuQueueKind::transfer:
        return;
    }
    throw std::invalid_argument{"unknown GPU queue"};
}

SubmissionRecoveryRuntime::Admission SubmissionRecoveryRuntime::admit()
{
    if (device_lost_)
    {
        throw std::logic_error{"GPU device is lost; new admissions blocked"};
    }
    return runtime_.admit();
}

std::shared_ptr<const RuntimeSnapshot> SubmissionRecoveryRuntime::acquire() const noexcept
{
    return runtime_.acquire();
}

void SubmissionRecoveryRuntime::publish(const config::EngineConfig& next, ResourceFactory& factory)
{
    if (device_lost_)
    {
        throw std::logic_error{"GPU device is lost; publication blocked"};
    }
    runtime_.publish(next, factory);
}

SubmissionResult SubmissionRecoveryRuntime::try_submit(Admission ticket, const GpuQueueKind queue,
                                                       SubmissionBackend& backend)
{
    if (device_lost_)
    {
        throw std::logic_error{"GPU device is lost; submission blocked"};
    }
    if (!ticket.snapshot())
    {
        throw std::logic_error{"admission already resolved"};
    }
    validate_queue(queue);
    if (next_incident_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error{"recovery incident IDs exhausted"};
    }

    // Reserving capacity is the ONLY potentially allocating operation in the
    // recovery-record path. Do it BEFORE the external backend call.
    incidents_.reserve(incidents_.size() + 1);
    const auto next_id = next_incident_ + 1;

    BackendSubmitOutcome outcome = BackendSubmitOutcome::unknown;
    try
    {
        outcome = backend.try_submit(queue, *ticket.snapshot());
    }
    catch (...)
    {
        // A backend exception alone cannot prove that nothing was enqueued.
        outcome = BackendSubmitOutcome::unknown;
    }

    if (outcome == BackendSubmitOutcome::not_enqueued)
    {
        ticket.cancel();
        return {SubmissionDisposition::rejected};
    }
    if (outcome == BackendSubmitOutcome::accepted)
    {
        try
        {
            return {SubmissionDisposition::accepted, ticket.submit(queue)};
        }
        catch (...)
        {
            // Acceptance is already known: never cancel the pending admission.
            // The caller must retry registration via confirm_accepted().
            incidents_.push_back(Incident{next_id, queue, true, std::move(ticket)});
            next_incident_ = next_id;
            return {SubmissionDisposition::registration_pending, 0, next_id};
        }
    }

    if (outcome == BackendSubmitOutcome::device_lost)
    {
        device_lost_ = true;
    }

    // Unknown acceptance MUST retain the ticket across publication/collection.
    incidents_.push_back(Incident{next_id, queue, false, std::move(ticket)});
    next_incident_ = next_id;
    return {outcome == BackendSubmitOutcome::device_lost ? SubmissionDisposition::device_lost
                                                         : SubmissionDisposition::unresolved,
            0, next_id};
}

std::vector<SubmissionRecoveryRuntime::Incident>::iterator
SubmissionRecoveryRuntime::find_incident(const std::uint64_t incident_id)
{
    const auto found =
        std::find_if(incidents_.begin(), incidents_.end(), [incident_id](const Incident& incident)
                     { return incident.id == incident_id; });
    if (found == incidents_.end())
    {
        throw std::invalid_argument{"unknown recovery incident"};
    }
    return found;
}

std::uint64_t SubmissionRecoveryRuntime::confirm_accepted(const std::uint64_t incident_id)
{
    const auto found = find_incident(incident_id);
    // An exception from registration preserves the incident and its ticket.
    const auto serial = found->ticket.submit(found->queue);
    incidents_.erase(found);
    return serial;
}

void SubmissionRecoveryRuntime::confirm_not_enqueued(const std::uint64_t incident_id)
{
    const auto found = find_incident(incident_id);
    if (found->accepted_by_backend)
    {
        throw std::logic_error{"backend already confirmed acceptance"};
    }
    found->ticket.cancel();
    incidents_.erase(found);
}

void SubmissionRecoveryRuntime::complete_through(const GpuQueueKind queue,
                                                 const std::uint64_t submission)
{
    if (device_lost_)
    {
        throw std::logic_error{"device loss makes simulated completion unreliable"};
    }
    runtime_.complete_through(queue, submission);
}

std::size_t SubmissionRecoveryRuntime::collect()
{
    if (device_lost_)
    {
        // Fail closed. This simulator does not claim a device reset or idle proof.
        return 0;
    }
    return runtime_.collect();
}

std::uint64_t SubmissionRecoveryRuntime::submitted(const GpuQueueKind queue) const
{
    return runtime_.submitted(queue);
}

std::size_t SubmissionRecoveryRuntime::pending_admissions() const
{
    return runtime_.pending_admissions();
}

std::size_t SubmissionRecoveryRuntime::unresolved_count() const noexcept
{
    return incidents_.size();
}

bool SubmissionRecoveryRuntime::device_lost() const noexcept
{
    return device_lost_;
}

} // namespace cpp_mastery::runtime
