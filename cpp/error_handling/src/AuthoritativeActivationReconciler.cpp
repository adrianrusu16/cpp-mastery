#include <cpp_mastery/runtime/AuthoritativeActivationReconciler.hpp>

#include <stdexcept>

namespace cpp_mastery::runtime
{

AuthoritativeActivationReconciler::AuthoritativeActivationReconciler(
    DurableActivationJournal& journal) noexcept
    : journal_{journal}
{
}

ReconciliationResult
AuthoritativeActivationReconciler::reconcile(const ActivationAttemptId id,
                                             ActivationStatusAuthority& authority)
{
    // Inspect durable state BEFORE asking the backend anything. A missing ID or
    // an already-terminal record must never trigger an external status query.
    const auto recorded = journal_.lookup(id);
    if (recorded.state != JournalActivationState::pending)
    {
        return {recorded, false};
    }

    ActivationStatusReply reply{};
    try
    {
        reply = authority.query(id, recorded.requested_generation);
    }
    catch (...)
    {
        // Connectivity failures, exceptions and temporary backend outages
        // are NOT authoritative evidence that no activation occurred.
        return {recorded, true};
    }

    if (reply.id != id || reply.generation != recorded.requested_generation)
    {
        throw std::invalid_argument{"status reply does not match journaled attempt"};
    }

    switch (reply.status)
    {
    case AuthoritativeActivationStatus::unavailable:
        return {recorded, true};
    case AuthoritativeActivationStatus::started:
        return {journal_.confirm(id, ActivationResolution::confirmed_started), true};
    case AuthoritativeActivationStatus::not_started:
        return {journal_.confirm(id, ActivationResolution::confirmed_not_started), true};
    }
    throw std::invalid_argument{"invalid authoritative activation status"};
}

} // namespace cpp_mastery::runtime
