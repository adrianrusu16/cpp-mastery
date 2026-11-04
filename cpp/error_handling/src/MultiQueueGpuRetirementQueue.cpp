#include <cpp_mastery/runtime/MultiQueueGpuRetirementQueue.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cpp_mastery::runtime
{

std::size_t MultiQueueGpuRetirementQueue::queue_index(const GpuQueueKind queue)
{
    switch (queue)
    {
    case GpuQueueKind::graphics:
        return 0;
    case GpuQueueKind::compute:
        return 1;
    case GpuQueueKind::transfer:
        return 2;
    }
    throw std::invalid_argument{"unknown GPU queue kind"};
}

std::vector<MultiQueueGpuRetirementQueue::Record>::iterator
MultiQueueGpuRetirementQueue::find(const SnapshotHandle& snapshot) noexcept
{
    return std::find_if(records_.begin(), records_.end(), [&snapshot](const Record& record)
                        { return record.snapshot.get() == snapshot.get(); });
}

bool MultiQueueGpuRetirementQueue::is_sealed(const SnapshotHandle& snapshot) const noexcept
{
    const std::weak_ptr<const RuntimeSnapshot> candidate{snapshot};
    return std::any_of(
        sealed_.begin(), sealed_.end(),
        [&candidate](const std::weak_ptr<const RuntimeSnapshot>& sealed)
        { return !sealed.owner_before(candidate) && !candidate.owner_before(sealed); });
}

MultiQueueGpuRetirementQueue::SubmissionId
MultiQueueGpuRetirementQueue::submit(const GpuQueueKind queue, const SnapshotHandle& snapshot)
{
    const std::size_t index = queue_index(queue);
    if (!snapshot)
    {
        throw std::invalid_argument{"cannot submit a null snapshot"};
    }

    auto record = find(snapshot);
    if (is_sealed(snapshot) || (record != records_.end() && record->retired))
    {
        throw std::logic_error{"cannot submit a retired snapshot"};
    }

    Timeline& timeline = timelines_[index];
    if (timeline.submitted == std::numeric_limits<SubmissionId>::max())
    {
        throw std::overflow_error{"GPU submission serial exhausted"};
    }

    const SubmissionId next = timeline.submitted + 1;
    if (record == records_.end())
    {
        Record fresh;
        fresh.snapshot = snapshot;
        fresh.last_use[index] = next;
        records_.push_back(std::move(fresh));
    }
    else
    {
        record->last_use[index] = next;
    }
    timeline.submitted = next;
    return next;
}

void MultiQueueGpuRetirementQueue::retire(const SnapshotHandle& snapshot)
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
        records_.push_back(Record{.snapshot = snapshot});
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

    if (newly_tracked)
    {
        records_.back().retired = true;
    }
    else
    {
        record->retired = true;
    }
}

void MultiQueueGpuRetirementQueue::complete_through(const GpuQueueKind queue,
                                                    const SubmissionId submission)
{
    Timeline& timeline = timelines_[queue_index(queue)];
    if (submission < timeline.completed || submission > timeline.submitted)
    {
        throw std::invalid_argument{"completion must advance within this queue's submissions"};
    }
    timeline.completed = submission;
}

MultiQueueGpuRetirementQueue::SubmissionId
MultiQueueGpuRetirementQueue::submitted(const GpuQueueKind queue) const
{
    return timelines_[queue_index(queue)].submitted;
}

MultiQueueGpuRetirementQueue::SubmissionId
MultiQueueGpuRetirementQueue::completed(const GpuQueueKind queue) const
{
    return timelines_[queue_index(queue)].completed;
}

bool MultiQueueGpuRetirementQueue::ready(const Record& record) const noexcept
{
    // A queue with no recorded use has a zero requirement. All other queues
    // must independently complete their last use before retirement is safe.
    for (std::size_t index = 0; index < queue_count; ++index)
    {
        if (record.last_use[index] > timelines_[index].completed)
        {
            return false;
        }
    }
    return true;
}

std::size_t MultiQueueGpuRetirementQueue::collect()
{
    const std::size_t collected = std::erase_if(records_, [this](const Record& record)
                                                { return record.retired && ready(record); });
    std::erase_if(sealed_, [](const std::weak_ptr<const RuntimeSnapshot>& handle)
                  { return handle.expired(); });
    return collected;
}

} // namespace cpp_mastery::runtime
