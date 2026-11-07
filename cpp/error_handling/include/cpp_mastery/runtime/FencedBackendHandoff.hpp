#pragma once

#include <cpp_mastery/runtime/FencedActivationReconciler.hpp>

#include <cstdint>
#include <mutex>

namespace cpp_mastery::runtime
{

// Submission is admitted at the backend boundary, not at the status-query layer.
// The trusted backend must route every protected effect through this gate.
struct FencedBackendCommand
{
    AuthorityEpoch owner;
    std::uint64_t command_id = 0;
};

enum class BackendAdmission
{
    executed,
    rejected_stale,
    rejected_quarantined,
};

enum class HandoffDisposition
{
    advanced,
    already_durable,
};

class BackendCommandSink
{
  public:
    virtual ~BackendCommandSink() = default;
    virtual void execute(FencedBackendCommand command) = 0;
};

// Optional failpoint: models a crash/lost acknowledgement AFTER durable fsync.
class HandoffAckHook
{
  public:
    virtual ~HandoffAckHook() = default;
    virtual void after_persist(AuthorityEpoch epoch) = 0;
};

// Linux teaching boundary, scoped to ONE trusted backend gate and one ledger.
// Both submission admission + effect execution and ownership advancement hold
// the same mutex: no old callback can overlap a *completed* handoff.
// Ledger fsync precedes the new owner being admitted. On an uncertain ledger
// write error, quarantine all operations until restart/ledger inspection.
// No cross-process shared lock, GPU fencing, or exactly-once operation log.
class FencedBackendGate final
{
  public:
    explicit FencedBackendGate(AuthorityEpochLedger& epochs) noexcept;
    FencedBackendGate(const FencedBackendGate&) = delete;
    FencedBackendGate& operator=(const FencedBackendGate&) = delete;

    [[nodiscard]] BackendAdmission submit(FencedBackendCommand command, BackendCommandSink& sink);
    [[nodiscard]] HandoffDisposition handoff(AuthorityEpoch next,
                                             HandoffAckHook* acknowledgement = nullptr);
    [[nodiscard]] bool quarantined() const;

  private:
    AuthorityEpochLedger& epochs_; // Borrowed; must outlive this gate.
    mutable std::mutex mutex_;
    bool quarantined_ = false;
};

} // namespace cpp_mastery::runtime
