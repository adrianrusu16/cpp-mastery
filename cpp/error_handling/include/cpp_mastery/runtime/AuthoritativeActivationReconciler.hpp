#pragma once

#include <cpp_mastery/runtime/DurableActivationJournal.hpp>

namespace cpp_mastery::runtime
{

// Status must be obtained from the backend that owns this persistent attempt.
// 'unavailable' is not a negative acknowledgement and cannot authorize a retry.
enum class AuthoritativeActivationStatus
{
    started,
    not_started,
    unavailable,
};

struct ActivationStatusReply
{
    ActivationAttemptId id;
    BackendGeneration generation;
    AuthoritativeActivationStatus status;
};

class ActivationStatusAuthority
{
  public:
    virtual ~ActivationStatusAuthority() = default;
    [[nodiscard]] virtual ActivationStatusReply query(ActivationAttemptId id,
                                                      BackendGeneration generation) = 0;
};

struct ReconciliationResult
{
    JournalActivationResult journal;
    bool queried_authority = false;
};

// CPU-only recovery state machine over Lesson 17's durable journal.
// One caller serializes journal access and authority callbacks. A terminal
// result is never fabricated from an unavailable, throwing or mismatched
// authority response; the original pending intent remains durable.
// A confirmed 'started' observation does NOT reconstruct a GPU device or prove
// any underlying GPU allocations safe for destruction.
class AuthoritativeActivationReconciler final
{
  public:
    explicit AuthoritativeActivationReconciler(DurableActivationJournal& journal) noexcept;

    // Terminal records are returned as-is; the authority is not queried again.
    // Pending records require trusted evidence, correlated with BOTH the
    // attempt ID and generation before journal.confirm() is permitted.
    [[nodiscard]] ReconciliationResult reconcile(ActivationAttemptId id,
                                                 ActivationStatusAuthority& authority);

  private:
    DurableActivationJournal& journal_; // Borrowed; journal outlives reconciler.
};

} // namespace cpp_mastery::runtime
