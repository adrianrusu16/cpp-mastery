#include <cpp_mastery/runtime/GpuRetirementQueue.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cpp_mastery::runtime
{

std::vector<GpuRetirementQueue::Record>::iterator
GpuRetirementQueue::find(const SnapshotHandle& snapshot) noexcept
{
    return std::find_if(records_.begin(), records_.end(), [&snapshot](const Record& record)
                        { return record.snapshot.get() == snapshot.get(); });
}

bool GpuRetirementQueue::is_sealed(const SnapshotHandle& snapshot) const noexcept
{
    const std::weak_ptr<const RuntimeSnapshot> candidate{snapshot};
    return std::any_of(
        sealed_.begin(), sealed_.end(),
        [&candidate](const std::weak_ptr<const RuntimeSnapshot>& sealed)
        { return !sealed.owner_before(candidate) && !candidate.owner_before(sealed); });
}

GpuRetirementQueue::SubmissionId GpuRetirementQueue::submit(const SnapshotHandle& snapshot)
{
    if (!snapshot)
    {
        throw std::invalid_argument{"cannot submit a null snapshot"};
    }

    auto record = find(snapshot);
    if (is_sealed(snapshot) || (record != records_.end() && record->retired))
    {
        throw std::logic_error{"cannot submit an already retired snapshot"};
    }
    if (submitted_ == std::numeric_limits<SubmissionId>::max())
    {
        throw std::overflow_error{"GPU submission serial exhausted"};
    }

    const SubmissionId next = submitted_ + 1;
    if (record == records_.end())
    {
        // Keep the old counter on a vector allocation failure.
        records_.push_back(Record{snapshot, next, false});
    }
    else
    {
        record->last_use = next;
    }
    submitted_ = next;
    return next;
}

void GpuRetirementQueue::retire(const SnapshotHandle& snapshot)
{
    if (!snapshot)
    {
        throw std::invalid_argument{"cannot retire a null snapshot"};
    }

    auto record = find(snapshot);
    if (is_sealed(snapshot) || (record != records_.end() && record->retired))
    {
        throw std::logic_error{"snapshot already retired"};
    }

    const bool newly_tracked = record == records_.end();
    if (newly_tracked)
    {
        // No GPU use was recorded: no completion signal is needed.
        records_.push_back(Record{snapshot, 0, false});
    }
    try
    {
        sealed_.push_back(snapshot);
    }
    catch (...)
    {
        if (newly_tracked)
        {
            records_.pop_back();
        }
        throw;
    }
    // The two potentially allocating operations succeeded; seal without throwing.
    if (newly_tracked)
    {
        records_.back().retired = true;
    }
    else
    {
        record->retired = true;
    }
}

void GpuRetirementQueue::complete_through(const SubmissionId submission)
{
    if (submission < completed_ || submission > submitted_)
    {
        throw std::invalid_argument{"completion must advance within issued submissions"};
    }
    completed_ = submission;
}

std::size_t GpuRetirementQueue::collect()
{
    // A sealed resource is eligible only after its final GPU submission completes.
    // std::erase_if visits every record, regardless of retirement order.
    const std::size_t collected =
        std::erase_if(records_, [this](const Record& record)
                      { return record.retired && record.last_use <= completed_; });
    // Do not keep control blocks of already destroyed snapshots indefinitely.
    std::erase_if(sealed_, [](const std::weak_ptr<const RuntimeSnapshot>& handle)
                  { return handle.expired(); });
    return collected;
}

} // namespace cpp_mastery::runtime
