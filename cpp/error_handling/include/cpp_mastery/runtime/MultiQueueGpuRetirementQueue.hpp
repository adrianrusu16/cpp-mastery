#pragma once

#include <cpp_mastery/runtime/SnapshotRuntime.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cpp_mastery::runtime
{

enum class GpuQueueKind
{
    graphics,
    compute,
    transfer,
};

// CPU-only model of THREE independent ordered completion domains.
// One controller serializes submit(), retire(), complete_through(), and collect().
// Callers stop new submissions before retiring a snapshot; GPU synchronization
// and actual Vulkan command execution are outside this teaching model.
class MultiQueueGpuRetirementQueue final
{
  public:
    using SnapshotHandle = std::shared_ptr<const RuntimeSnapshot>;
    using SubmissionId = std::uint64_t;

    // Each queue issues its OWN monotonically increasing serial numbers.
    // A failed record allocation does not advance that queue's serial.
    [[nodiscard]] SubmissionId submit(GpuQueueKind queue, const SnapshotHandle& snapshot);

    // Seal against further uses. An unused snapshot has no GPU dependencies.
    // May allocate; failure leaves the snapshot unretired.
    void retire(const SnapshotHandle& snapshot);

    // Completion may advance independently on each GPU queue.
    void complete_through(GpuQueueKind queue, SubmissionId submission);

    // Release retired entries only when ALL queues that used them have completed.
    // Remaining CPU shared_ptr owners can extend object lifetime further.
    [[nodiscard]] std::size_t collect();

    [[nodiscard]] SubmissionId submitted(GpuQueueKind queue) const;
    [[nodiscard]] SubmissionId completed(GpuQueueKind queue) const;
    [[nodiscard]] std::size_t tracked_count() const noexcept
    {
        return records_.size();
    }

  private:
    static constexpr std::size_t queue_count = 3;
    using Uses = std::array<SubmissionId, queue_count>;

    struct Timeline
    {
        SubmissionId submitted = 0;
        SubmissionId completed = 0;
    };

    struct Record
    {
        SnapshotHandle snapshot;
        Uses last_use{};
        bool retired = false;
    };

    [[nodiscard]] static std::size_t queue_index(GpuQueueKind queue);
    [[nodiscard]] std::vector<Record>::iterator find(const SnapshotHandle& snapshot) noexcept;
    [[nodiscard]] bool is_sealed(const SnapshotHandle& snapshot) const noexcept;
    [[nodiscard]] bool ready(const Record& record) const noexcept;

    std::array<Timeline, queue_count> timelines_{};
    std::vector<Record> records_;
    // Non-owning tombstones reject reuse after collection while CPU owners live.
    std::vector<std::weak_ptr<const RuntimeSnapshot>> sealed_;
};

} // namespace cpp_mastery::runtime
