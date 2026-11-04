#include <cpp_mastery/runtime/CrossQueueHandoff.hpp>

#include <limits>
#include <stdexcept>

namespace cpp_mastery::runtime
{

CrossQueueHandoff::CrossQueueHandoff(const HandoffSharing sharing,
                                     const std::uint32_t transfer_family,
                                     const std::uint32_t graphics_family) noexcept
    : sharing_{sharing}, transfer_family_{transfer_family}, graphics_family_{graphics_family}
{
}

bool CrossQueueHandoff::ownership_transfer_required() const noexcept
{
    return sharing_ == HandoffSharing::exclusive && transfer_family_ != graphics_family_;
}

CrossQueueHandoff::SubmissionId CrossQueueHandoff::transfer_write()
{
    if (latest_write_ == std::numeric_limits<SubmissionId>::max())
    {
        throw std::overflow_error{"transfer submission serial exhausted"};
    }
    return ++latest_write_;
}

void CrossQueueHandoff::signal_transfer(const SubmissionId value)
{
    if (value == 0 || value > latest_write_ || value < signaled_)
    {
        throw std::invalid_argument{"signal must be monotonic and reference a recorded write"};
    }
    signaled_ = value;
}

void CrossQueueHandoff::wait_graphics(const SubmissionId value, const HandoffWaitStage stage)
{
    if (value == 0 || value > signaled_)
    {
        throw std::invalid_argument{"wait requires an associated transfer signal"};
    }
    switch (stage)
    {
    case HandoffWaitStage::vertex_input:
    case HandoffWaitStage::fragment_shader:
    case HandoffWaitStage::all_commands:
        break;
    default:
        throw std::invalid_argument{"unsupported simulated wait stage"};
    }
    waited_ = value;
    wait_stage_ = stage;
}

void CrossQueueHandoff::release_to_graphics(const SubmissionId value)
{
    if (!ownership_transfer_required())
    {
        throw std::logic_error{"no queue-family ownership transfer is required"};
    }
    if (value == 0 || value != latest_write_)
    {
        throw std::invalid_argument{"release must reference the latest transfer write"};
    }
    if (released_ == value)
    {
        throw std::logic_error{"duplicate ownership release"};
    }
    released_ = value;
}

void CrossQueueHandoff::acquire_on_graphics(const SubmissionId value)
{
    if (!ownership_transfer_required())
    {
        throw std::logic_error{"no queue-family ownership transfer is required"};
    }
    if (value == 0 || value != latest_write_ || released_ != value)
    {
        throw std::logic_error{"matching transfer-queue release is required"};
    }
    // This model assumes the default ownership-transfer synchronization path:
    // absent the ALL_STAGES extension, use ALL_COMMANDS for semaphore waits.
    if (waited_ < value || wait_stage_ != HandoffWaitStage::all_commands)
    {
        throw std::logic_error{"ownership acquire requires a matching all-commands wait"};
    }
    if (acquired_ == value)
    {
        throw std::logic_error{"duplicate ownership acquire"};
    }
    acquired_ = value;
}

bool CrossQueueHandoff::graphics_read_ready() const noexcept
{
    // A semaphore dependency must cover the latest write and the consumer
    // stage. Independent GPU queues have no implicit execution order.
    if (latest_write_ == 0 || signaled_ < latest_write_ || waited_ < latest_write_)
    {
        return false;
    }
    if (wait_stage_ != HandoffWaitStage::vertex_input &&
        wait_stage_ != HandoffWaitStage::all_commands)
    {
        return false;
    }
    // Exclusive sharing across queue families additionally needs matching
    // release/acquire operations. The default path uses ALL_COMMANDS.
    return !ownership_transfer_required() ||
           (released_ == latest_write_ && acquired_ == latest_write_ &&
            wait_stage_ == HandoffWaitStage::all_commands);
}

CrossQueueHandoff::SubmissionId CrossQueueHandoff::graphics_read()
{
    if (!graphics_read_ready())
    {
        throw std::logic_error{"graphics read lacks a complete cross-queue dependency"};
    }
    ++reads_;
    return latest_write_;
}

} // namespace cpp_mastery::runtime
