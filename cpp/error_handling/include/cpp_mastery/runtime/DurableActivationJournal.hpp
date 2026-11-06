#pragma once

#include <cpp_mastery/runtime/IdempotentActivationRuntime.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>

namespace cpp_mastery::runtime
{

// A journal records observations, not a reconstructed GPU device or a durable
// proof that the old backend is safe to destroy.
enum class JournalActivationState
{
    pending,
    started,
    not_started,
};

struct JournalActivationResult
{
    ActivationAttemptId id;
    BackendGeneration requested_generation;
    JournalActivationState state;
};

// The backend must correlate the persistent attempt ID with its own operation.
// A thrown callback is an UNKNOWN outcome, never proof that no work started.
class JournalActivationBackend
{
  public:
    virtual ~JournalActivationBackend() = default;
    [[nodiscard]] virtual ActivationOutcome activate(ActivationAttemptId id,
                                                     BackendGeneration generation,
                                                     const config::EngineConfig& next) = 0;
};

// Deterministic fault injection BEFORE a journal record is written.
// Production callers can omit this; it is useful for simulating crashes and
// disk errors at the intent/terminal record boundaries.
class JournalWriteHook
{
  public:
    virtual ~JournalWriteHook() = default;
    virtual void before_append(char record_type) = 0;
};

// Linux/POSIX teaching model: one writer, protected with an exclusive advisory
// file lock. Each appended record is write()+fsync()'d BEFORE invoking any
// external backend. The directory is fsync()'d when the journal is created.
// Reopening validates every record and rejects torn/corrupt histories closed.
// A pending record after restart MUST be reconciled with external authority;
// it must never trigger an automatic replay of the backend operation.
class DurableActivationJournal final
{
  public:
    explicit DurableActivationJournal(const std::filesystem::path& path,
                                      JournalWriteHook* hook = nullptr);
    ~DurableActivationJournal();
    DurableActivationJournal(const DurableActivationJournal&) = delete;
    DurableActivationJournal& operator=(const DurableActivationJournal&) = delete;
    DurableActivationJournal(DurableActivationJournal&&) = delete;
    DurableActivationJournal& operator=(DurableActivationJournal&&) = delete;

    [[nodiscard]] JournalActivationResult begin_or_replay(ActivationAttemptId id,
                                                          BackendGeneration generation,
                                                          const config::EngineConfig& next,
                                                          JournalActivationBackend& backend);

    // Only call after independent authoritative reconciliation, including after
    // process restart. This API cannot establish GPU/device teardown safety.
    [[nodiscard]] JournalActivationResult confirm(ActivationAttemptId id,
                                                  ActivationResolution resolution);
    [[nodiscard]] JournalActivationResult lookup(ActivationAttemptId id) const;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    struct Entry
    {
        BackendGeneration generation;
        config::EngineConfig config;
        JournalActivationState state = JournalActivationState::pending;
    };

    static void validate(ActivationAttemptId id, BackendGeneration generation);
    static JournalActivationResult result(ActivationAttemptId id, const Entry& entry) noexcept;
    void load();
    void append(char kind, ActivationAttemptId id, const Entry& entry);
    void finish(ActivationAttemptId id, Entry& entry, JournalActivationState state);
    [[nodiscard]] bool has_pending() const noexcept;

    int fd_ = -1;
    JournalWriteHook* hook_ = nullptr; // Borrowed; must outlive this journal.
    std::map<std::uint64_t, Entry> entries_;
    bool poisoned_ = false;
};

} // namespace cpp_mastery::runtime
