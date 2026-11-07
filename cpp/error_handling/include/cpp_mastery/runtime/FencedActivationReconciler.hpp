#pragma once

#include <cpp_mastery/runtime/AuthoritativeActivationReconciler.hpp>

#include <cstdint>
#include <filesystem>

namespace cpp_mastery::runtime
{

// The identity of the authority owner, not a GPU submission timeline.
// Ownership transitions must be authorized by the external coordinator.
struct AuthorityEpoch
{
    std::uint64_t value = 0;
    friend bool operator==(AuthorityEpoch, AuthorityEpoch) = default;
};

class EpochWriteHook
{
  public:
    virtual ~EpochWriteHook() = default;
    virtual void before_append(AuthorityEpoch next) = 0;
};

// POSIX/Linux teaching model: a single durable monotonic epoch per journal.
// Uses an exclusive file lock, append-only checksummed records, and fsync.
// An empty ledger has epoch 0 and cannot authorize any reconciliation.
class AuthorityEpochLedger final
{
  public:
    explicit AuthorityEpochLedger(const std::filesystem::path& path,
                                  EpochWriteHook* hook = nullptr);
    ~AuthorityEpochLedger();
    AuthorityEpochLedger(const AuthorityEpochLedger&) = delete;
    AuthorityEpochLedger& operator=(const AuthorityEpochLedger&) = delete;
    AuthorityEpochLedger(AuthorityEpochLedger&&) = delete;
    AuthorityEpochLedger& operator=(AuthorityEpochLedger&&) = delete;

    [[nodiscard]] AuthorityEpoch current() const noexcept;
    // Only a privileged ownership coordinator may advance the fence.
    // Persists the new epoch BEFORE a new owner can reconcile anything.
    void advance(AuthorityEpoch next);

  private:
    void load();
    void append(AuthorityEpoch next);

    int fd_ = -1;
    AuthorityEpoch current_{};
    EpochWriteHook* hook_ = nullptr; // Borrowed; must outlive the ledger.
    bool poisoned_ = false;
};

struct FencedStatusReply
{
    ActivationAttemptId id;
    BackendGeneration generation;
    AuthorityEpoch epoch;
    AuthoritativeActivationStatus status;
};

class FencedStatusAuthority
{
  public:
    virtual ~FencedStatusAuthority() = default;
    [[nodiscard]] virtual FencedStatusReply
    query(ActivationAttemptId id, BackendGeneration generation, AuthorityEpoch epoch) = 0;
};

struct FencedReconciliationResult
{
    JournalActivationResult journal;
    bool queried_authority = false;
    bool rejected_stale = false;
};

// The journal owns durable operation state; the ledger owns durable authority
// identity. ONE controller serializes calls, including authority callbacks and
// ledger updates. The post-query epoch check closes the callback handoff gap.
// A real deployment must fence writes at the actual external backend, too.
class FencedActivationReconciler final
{
  public:
    FencedActivationReconciler(DurableActivationJournal& journal,
                               AuthorityEpochLedger& epochs) noexcept;
    [[nodiscard]] FencedReconciliationResult reconcile(ActivationAttemptId id,
                                                       FencedStatusAuthority& authority);

  private:
    DurableActivationJournal& journal_;
    AuthorityEpochLedger& epochs_;
};

} // namespace cpp_mastery::runtime
