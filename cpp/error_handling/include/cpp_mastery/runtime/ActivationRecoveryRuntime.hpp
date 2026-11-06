#pragma once

#include <cpp_mastery/runtime/TransactionalBackendRuntime.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace cpp_mastery::runtime
{

// This is a CPU-only model. 'unknown' never proves that the backend stopped.
enum class ActivationOutcome
{
    started,
    not_started,
    unknown,
};

enum class ActivationResolution
{
    confirmed_started,
    confirmed_not_started,
};

class ActivatableBackend : public ReconstructionBackend
{
  public:
    ~ActivatableBackend() override = default;
    // May throw; an exception has an UNKNOWN outcome, not a confirmed rejection.
    [[nodiscard]] virtual ActivationOutcome activate() = 0;
};

class ActivationBuilder : public ReconstructionBuilder
{
  public:
    ~ActivationBuilder() override = default;
    [[nodiscard]] virtual std::shared_ptr<ActivatableBackend>
    build_activatable(BackendGeneration generation) = 0;

    [[nodiscard]] std::shared_ptr<ReconstructionBackend> build(BackendGeneration generation) final
    {
        return build_activatable(generation);
    }
};

// One-controller model: all operations, callbacks and confirmations are serialized.
// The owner MUST retain this object while activation is unresolved. Destroying a
// real potentially active backend without an external safety proof is forbidden.
class ActivationRecoveryRuntime final
{
  public:
    ActivationRecoveryRuntime(const config::EngineConfig& initial, ActivationBuilder& builder);
    ~ActivationRecoveryRuntime();

    [[nodiscard]] BackendGeneration generation() const noexcept;
    [[nodiscard]] bool device_lost() const noexcept;
    [[nodiscard]] bool activation_pending() const noexcept;
    [[nodiscard]] std::shared_ptr<const RuntimeSnapshot> acquire() const noexcept;
    void publish(BackendGeneration generation, const config::EngineConfig& next);
    void report_device_lost(BackendGeneration generation);
    [[nodiscard]] SubmissionResult try_submit(BackendGeneration generation, GpuQueueKind queue,
                                              SubmissionBackend& backend);
    void complete_through(BackendGeneration generation, GpuQueueKind queue,
                          std::uint64_t submission);
    [[nodiscard]] std::uint64_t submitted(GpuQueueKind queue) const;
    [[nodiscard]] std::size_t collect();
    [[nodiscard]] std::size_t unresolved_count() const noexcept;

    // No publication unless activation is CONFIRMED to have started. A confirmed
    // no-start throws after dropping staged state; unknown returns nullopt and
    // keeps staging alive for external resolution.
    [[nodiscard]] std::optional<BackendGeneration> recover(BackendGeneration old_generation,
                                                           const config::EngineConfig& next,
                                                           ActivationBuilder& builder,
                                                           SafeTeardownAuthority& authority);

    // An external authority resolves a previously unknown activation outcome.
    // Confirmed no-start discards staging, so a later recover() can retry.
    // Confirmed started attempts to publish the staged generation.
    [[nodiscard]] std::optional<BackendGeneration> resolve_pending(ActivationResolution result);

  private:
    class PreparedBackend;
    struct Pending;
    [[nodiscard]] BackendGeneration commit_pending();

    std::unique_ptr<TransactionalBackendRuntime> runtime_;
    std::unique_ptr<Pending> pending_;
};

} // namespace cpp_mastery::runtime
