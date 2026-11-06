#pragma once

#include <cpp_mastery/runtime/ActivationRecoveryRuntime.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

namespace cpp_mastery::runtime
{

// Process-local caller-supplied identity. Reusing an identity for a different
// generation or configuration is an error; use a fresh identity for a NEW attempt.
struct ActivationAttemptId
{
    std::uint64_t value = 0;
    friend bool operator==(ActivationAttemptId, ActivationAttemptId) = default;
};

enum class ActivationAttemptState
{
    pending,
    committed,
    not_started,
};

struct ActivationAttemptResult
{
    ActivationAttemptId id;
    ActivationAttemptState state;
    BackendGeneration generation{}; // Nonzero only for a committed generation.
};

// Single-controller CPU-only teaching model. A request identity is reserved
// BEFORE any external builder/activation call. Replays are read-only and do
// not call the backend or teardown authority again.
// Entries are deliberately retained for this object's lifetime. This is NOT a
// persistent exactly-once protocol across crashes, nor a real Vulkan adapter.
class IdempotentActivationRuntime final
{
  public:
    IdempotentActivationRuntime(const config::EngineConfig& initial, ActivationBuilder& builder);

    [[nodiscard]] BackendGeneration generation() const noexcept;
    [[nodiscard]] bool device_lost() const noexcept;
    [[nodiscard]] bool activation_pending() const noexcept;
    [[nodiscard]] std::shared_ptr<const RuntimeSnapshot> acquire() const noexcept;
    void report_device_lost(BackendGeneration generation);
    [[nodiscard]] std::size_t recorded_attempts() const noexcept;

    // Initial not-started/preparation failures throw, but permanently consume
    // the identity; replay returns not_started and cannot activate twice.
    // An uncertain outcome returns pending until resolve() is called.
    [[nodiscard]] ActivationAttemptResult recover_once(ActivationAttemptId id,
                                                       BackendGeneration old_generation,
                                                       const config::EngineConfig& next,
                                                       ActivationBuilder& builder,
                                                       SafeTeardownAuthority& authority);

    // Replaying the SAME resolution is idempotent. A contradictory resolution
    // is rejected. A failed commit retains its pending identity for retry.
    [[nodiscard]] ActivationAttemptResult resolve(ActivationAttemptId id,
                                                  ActivationResolution resolution);

  private:
    struct Entry
    {
        BackendGeneration original;
        config::EngineConfig config;
        ActivationAttemptState state = ActivationAttemptState::pending;
        BackendGeneration committed{};
        std::optional<ActivationResolution> resolution;
    };

    [[nodiscard]] static ActivationAttemptResult result(ActivationAttemptId id,
                                                        const Entry& entry) noexcept;

    ActivationRecoveryRuntime runtime_;
    std::map<std::uint64_t, Entry> attempts_;
    std::optional<std::uint64_t> pending_id_;
};

} // namespace cpp_mastery::runtime
