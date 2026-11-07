#include <cpp_mastery/runtime/FencedBackendHandoff.hpp>

#include <stdexcept>

namespace cpp_mastery::runtime
{

FencedBackendGate::FencedBackendGate(AuthorityEpochLedger& epochs) noexcept : epochs_{epochs} {}

BackendAdmission FencedBackendGate::submit(const FencedBackendCommand command,
                                           BackendCommandSink& sink)
{
    if (command.command_id == 0)
    {
        throw std::invalid_argument{"backend command identity must be nonzero"};
    }

    std::lock_guard lock{mutex_};
    if (quarantined_)
    {
        return BackendAdmission::rejected_quarantined;
    }

    const AuthorityEpoch current = epochs_.current();
    if (command.owner.value == 0 || command.owner != current)
    {
        return BackendAdmission::rejected_stale;
    }

    // GREEN: only the current durable owner can pass the backend gate.
    // Keep the epoch check and the external effect within one critical section.
    sink.execute(command);
    return BackendAdmission::executed;
}

HandoffDisposition FencedBackendGate::handoff(const AuthorityEpoch next,
                                              HandoffAckHook* acknowledgement)
{
    std::lock_guard lock{mutex_};
    if (quarantined_)
    {
        throw std::logic_error{"handoff ledger state uncertain; reopen and inspect"};
    }
    const auto current = epochs_.current();
    if (next.value == 0 || next.value < current.value)
    {
        throw std::invalid_argument{"cannot hand off to a zero or older epoch"};
    }
    if (next == current)
    {
        if (next.value == 0)
        {
            throw std::invalid_argument{"zero epoch is not an owner"};
        }
        return HandoffDisposition::already_durable;
    }

    try
    {
        // A successful return guarantees the fence is on disk (fsync).
        epochs_.advance(next);
    }
    catch (...)
    {
        // A failed write/fsync may have partially persisted the epoch.
        // No further operation is admitted until restart and verification.
        quarantined_ = true;
        throw;
    }

    // An exception here models a lost acknowledgement after durable commit;
    // retrying the same epoch returns already_durable without another write.
    if (acknowledgement)
    {
        acknowledgement->after_persist(next);
    }
    return HandoffDisposition::advanced;
}

bool FencedBackendGate::quarantined() const
{
    std::lock_guard lock{mutex_};
    return quarantined_;
}

} // namespace cpp_mastery::runtime
