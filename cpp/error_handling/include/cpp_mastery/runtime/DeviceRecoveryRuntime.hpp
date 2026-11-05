#pragma once

#include <cpp_mastery/runtime/SubmissionRecovery.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace cpp_mastery::runtime
{

// Strongly typed identity: serial 1 on generation 2 is NOT serial 1 on generation 1.
struct BackendGeneration
{
    std::uint64_t value = 0;
    friend bool operator==(BackendGeneration, BackendGeneration) = default;
};

// Trusted boundary supplied by the GPU backend owner. Returning true asserts
// old-device use of its resources has ended and old GPU handles can be retired.
// A real Vulkan adapter MUST establish this through documented backend teardown
// guarantees; device loss or a newly created device alone is NOT such a proof.
class SafeTeardownAuthority
{
  public:
    virtual ~SafeTeardownAuthority() = default;
    virtual bool confirmed_safe_to_destroy(BackendGeneration old_generation) = 0;
};

// CPU-only generation coordinator. ONE controller serializes calls to this
// wrapper, including all external authority/backend interactions. Admissions
// never escape: try_submit() resolves or retains them in SubmissionRecoveryRuntime.
// IMPORTANT: the owner must keep this object alive while quarantined; disposing
// of a real GPU backend before safe teardown is outside this simulated API.
class DeviceRecoveryRuntime final
{
  public:
    DeviceRecoveryRuntime(const config::EngineConfig& initial, ResourceFactory& factory);

    [[nodiscard]] BackendGeneration generation() const noexcept;
    [[nodiscard]] bool device_lost() const noexcept;
    [[nodiscard]] std::shared_ptr<const RuntimeSnapshot> acquire() const noexcept;

    void publish(BackendGeneration generation, const config::EngineConfig& next,
                 ResourceFactory& factory);
    [[nodiscard]] SubmissionResult try_submit(BackendGeneration generation, GpuQueueKind queue,
                                              SubmissionBackend& backend);
    [[nodiscard]] std::uint64_t confirm_accepted(BackendGeneration generation,
                                                 std::uint64_t incident_id);
    void confirm_not_enqueued(BackendGeneration generation, std::uint64_t incident_id);
    void complete_through(BackendGeneration generation, GpuQueueKind queue,
                          std::uint64_t submission);
    [[nodiscard]] std::size_t collect();
    [[nodiscard]] std::uint64_t submitted(GpuQueueKind queue) const;
    [[nodiscard]] std::size_t unresolved_count() const noexcept;
    [[nodiscard]] std::size_t pending_admissions() const;

    // External detection (not necessarily from try_submit) freezes the generation.
    void report_device_lost(BackendGeneration generation);

    // Only a matching old generation AND a trusted safe-teardown confirmation
    // permit dropping quarantined ownership. New preparation may throw: old
    // generation remains frozen and intact, so the caller may retry.
    // The new generation has fresh, independent GPU timelines.
    [[nodiscard]] BackendGeneration recover(BackendGeneration old_generation,
                                            const config::EngineConfig& next,
                                            ResourceFactory& new_factory,
                                            SafeTeardownAuthority& authority);

  private:
    void require_generation(BackendGeneration generation) const;
    void require_running() const;

    std::unique_ptr<SubmissionRecoveryRuntime> active_;
    BackendGeneration generation_{1};
    bool frozen_ = false;
};

} // namespace cpp_mastery::runtime
