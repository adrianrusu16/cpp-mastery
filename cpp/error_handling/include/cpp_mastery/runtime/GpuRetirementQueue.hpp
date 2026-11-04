#pragma once

#include <cpp_mastery/runtime/SnapshotRuntime.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cpp_mastery::runtime
{

// CPU-only simulation of ONE ordered GPU submission/completion domain.
// One controller serializes submit(), retire(), complete_through(), and collect().
// Callers must stop new submissions for a snapshot BEFORE retiring it.
class GpuRetirementQueue final
{
  public:
    using SnapshotHandle = std::shared_ptr<const RuntimeSnapshot>;
    using SubmissionId = std::uint64_t;

    // Records a simulated GPU use, retaining ownership until retirement can complete.
    // May allocate; on failure the issued submission number does not advance.
    [[nodiscard]] SubmissionId submit(const SnapshotHandle& snapshot);

    // Seal this snapshot against further submissions. An unused snapshot has
    // last submission 0, so it is immediately eligible for collection.
    // May allocate for a snapshot not previously submitted.
    void retire(const SnapshotHandle& snapshot);

    // Ordered completion high-water mark. Rejects regression and future IDs.
    void complete_through(SubmissionId submission);

    // Release only sealed snapshots whose FINAL recorded use is complete.
    // Other CPU shared_ptr owners may keep their resources alive longer.
    [[nodiscard]] std::size_t collect();

    [[nodiscard]] SubmissionId submitted() const noexcept
    {
        return submitted_;
    }
    [[nodiscard]] SubmissionId completed() const noexcept
    {
        return completed_;
    }
    [[nodiscard]] std::size_t tracked_count() const noexcept
    {
        return records_.size();
    }

  private:
    struct Record
    {
        SnapshotHandle snapshot;
        SubmissionId last_use;
        bool retired;
    };

    [[nodiscard]] std::vector<Record>::iterator find(const SnapshotHandle& snapshot) noexcept;
    [[nodiscard]] bool is_sealed(const SnapshotHandle& snapshot) const noexcept;

    std::vector<Record> records_;
    // Weak tombstones reject late submissions even after completed resources are
    // collected, while allowing the CPU snapshot itself to be destroyed.
    std::vector<std::weak_ptr<const RuntimeSnapshot>> sealed_;
    SubmissionId submitted_ = 0;
    SubmissionId completed_ = 0;
};

} // namespace cpp_mastery::runtime
