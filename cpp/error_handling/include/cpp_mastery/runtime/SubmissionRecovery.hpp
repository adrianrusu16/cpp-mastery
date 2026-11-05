#pragma once

#include <cpp_mastery/runtime/CoordinatedRuntime.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cpp_mastery::runtime
{

// CPU-only simulation of the backend acceptance boundary. A throwing backend
// has an UNKNOWN outcome: a caller must not assume it enqueued no work.
enum class BackendSubmitOutcome
{
    accepted,
    not_enqueued,
    unknown,
    device_lost,
};

class SubmissionBackend
{
  public:
    virtual ~SubmissionBackend() = default;
    virtual BackendSubmitOutcome try_submit(GpuQueueKind queue,
                                            const RuntimeSnapshot& snapshot) = 0;
};

enum class SubmissionDisposition
{
    accepted,
    rejected,
    unresolved,
    registration_pending,
    device_lost,
};

struct SubmissionResult
{
    SubmissionDisposition disposition;
    std::uint64_t gpu_serial = 0;  // Nonzero only after successful GPU-use registration.
    std::uint64_t incident_id = 0; // Nonzero while an outcome requires resolution.
};

// Integrates Lesson 11's admission tickets with a simulated submit backend.
// ONE controller serializes calls to this wrapper; CoordinatedRuntime separately
// serializes publication and admission against GPU retirement.
// No Vulkan API is called. Do not destroy a real backend while work may be active.
class SubmissionRecoveryRuntime final
{
  public:
    using Admission = CoordinatedRuntime::Admission;

    SubmissionRecoveryRuntime(const config::EngineConfig& initial, ResourceFactory& factory);

    [[nodiscard]] Admission admit();
    [[nodiscard]] std::shared_ptr<const RuntimeSnapshot> acquire() const noexcept;
    void publish(const config::EngineConfig& next, ResourceFactory& factory);

    // Consumes a ticket, preallocating recovery bookkeeping BEFORE calling the
    // backend. Known rejection cancels; unknown outcome retains the admission.
    // Backend exceptions are treated conservatively as an unknown outcome.
    [[nodiscard]] SubmissionResult try_submit(Admission ticket, GpuQueueKind queue,
                                              SubmissionBackend& backend);

    // External confirmation is required: do not guess whether work was enqueued.
    // Confirmed acceptance registers a GPU dependency (and may throw); confirmed
    // no-enqueue cancels the reservation without creating a GPU serial.
    [[nodiscard]] std::uint64_t confirm_accepted(std::uint64_t incident_id);
    void confirm_not_enqueued(std::uint64_t incident_id);

    void complete_through(GpuQueueKind queue, std::uint64_t submission);
    [[nodiscard]] std::size_t collect();
    [[nodiscard]] std::uint64_t submitted(GpuQueueKind queue) const;
    [[nodiscard]] std::size_t pending_admissions() const;
    [[nodiscard]] std::size_t unresolved_count() const noexcept;
    [[nodiscard]] bool device_lost() const noexcept;

  private:
    struct Incident
    {
        std::uint64_t id;
        GpuQueueKind queue;
        bool accepted_by_backend;
        Admission ticket;
    };

    [[nodiscard]] std::vector<Incident>::iterator find_incident(std::uint64_t incident_id);
    static void validate_queue(GpuQueueKind queue);

    CoordinatedRuntime runtime_;
    std::vector<Incident> incidents_;
    std::uint64_t next_incident_ = 0;
    bool device_lost_ = false;
};

} // namespace cpp_mastery::runtime
