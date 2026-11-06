#pragma once

#include <cpp_mastery/runtime/DeviceRecoveryRuntime.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace cpp_mastery::runtime
{

// CPU-only reconstruction seam. A backend owns whatever is required to allocate
// its two snapshot resources; it does NOT represent a real Vulkan device.
class ReconstructionBackend : public ResourceFactory
{
  public:
    ~ReconstructionBackend() override = default;
    [[nodiscard]] virtual BackendGeneration generation() const noexcept = 0;
};

class ReconstructionBuilder
{
  public:
    virtual ~ReconstructionBuilder() = default;
    // May throw or return null. No external GPU work is submitted by build().
    [[nodiscard]] virtual std::shared_ptr<ReconstructionBackend>
    build(BackendGeneration generation) = 0;
};

// One-controller reconstruction model. All calls and external builder/authority
// interactions are serialized by the caller. Neither Vulkan nor real device
// teardown is implemented here.
//
// The buffer lease retains the backend even after a CPU reader outlives the
// coordinator. For a real backend, destruction is STILL subject to its genuine
// GPU safe-teardown requirements, not merely this shared_ptr lifetime.
class TransactionalBackendRuntime final
{
  public:
    TransactionalBackendRuntime(const config::EngineConfig& initial,
                                ReconstructionBuilder& builder);

    [[nodiscard]] BackendGeneration generation() const noexcept;
    [[nodiscard]] bool device_lost() const noexcept;
    [[nodiscard]] std::shared_ptr<const RuntimeSnapshot> acquire() const noexcept;

    void publish(BackendGeneration generation, const config::EngineConfig& next);
    [[nodiscard]] SubmissionResult try_submit(BackendGeneration generation, GpuQueueKind queue,
                                              SubmissionBackend& backend);
    void report_device_lost(BackendGeneration generation);
    void complete_through(BackendGeneration generation, GpuQueueKind queue,
                          std::uint64_t submission);
    [[nodiscard]] std::uint64_t submitted(GpuQueueKind queue) const;
    [[nodiscard]] std::size_t unresolved_count() const noexcept;
    [[nodiscard]] std::size_t collect();

    // Rebuild a backend AND both immutable-snapshot resources before publishing
    // a new generation. An exception leaves the old generation quarantined.
    // Authority confirms safe teardown for precisely old_generation, once per
    // attempt, before any new backend allocations. Failed attempts can retry.
    [[nodiscard]] BackendGeneration recover(BackendGeneration old_generation,
                                            const config::EngineConfig& next,
                                            ReconstructionBuilder& builder,
                                            SafeTeardownAuthority& authority);

  private:
    // Stored separately so the active backend remains available for publication
    // and all resource handles can retain backend leases independently.
    std::shared_ptr<ReconstructionBackend> active_backend_;
    std::unique_ptr<DeviceRecoveryRuntime> runtime_;
};

} // namespace cpp_mastery::runtime
