#pragma once

#include <cstdint>

namespace cpp_mastery::runtime
{

// CPU-only transfer -> graphics synchronization and ownership teaching model.
// This models one buffer, a single controller and two queue-family identities.
// It does not issue Vulkan commands or simulate GPU cache behavior.
enum class HandoffSharing
{
    exclusive,
    concurrent,
};

enum class HandoffWaitStage
{
    vertex_input,
    fragment_shader,
    all_commands,
};

class CrossQueueHandoff final
{
  public:
    using SubmissionId = std::uint64_t;

    CrossQueueHandoff(HandoffSharing sharing, std::uint32_t transfer_family,
                      std::uint32_t graphics_family) noexcept;

    // Record one logical transfer write. A new write requires a new dependency.
    [[nodiscard]] SubmissionId transfer_write();

    // Model a transfer-queue timeline signal associated with a recorded write.
    // The signal/wait pair provides the modeled execution and memory dependency.
    void signal_transfer(SubmissionId value);
    void wait_graphics(SubmissionId value, HandoffWaitStage stage);

    // Required only for exclusive sharing across DIFFERENT queue families.
    // The handoff models the default ALL_COMMANDS synchronization path; it
    // deliberately excludes optional ownership-transfer-all-stages extensions.
    void release_to_graphics(SubmissionId value);
    void acquire_on_graphics(SubmissionId value);

    [[nodiscard]] bool graphics_read_ready() const noexcept;
    // Read the latest write, rejecting unsynchronized reads.
    [[nodiscard]] SubmissionId graphics_read();

    [[nodiscard]] SubmissionId latest_write() const noexcept
    {
        return latest_write_;
    }
    [[nodiscard]] std::uint64_t reads() const noexcept
    {
        return reads_;
    }
    [[nodiscard]] bool ownership_transfer_required() const noexcept;

  private:
    HandoffSharing sharing_;
    std::uint32_t transfer_family_;
    std::uint32_t graphics_family_;
    SubmissionId latest_write_ = 0;
    SubmissionId signaled_ = 0;
    SubmissionId waited_ = 0;
    SubmissionId released_ = 0;
    SubmissionId acquired_ = 0;
    HandoffWaitStage wait_stage_ = HandoffWaitStage::fragment_shader;
    std::uint64_t reads_ = 0;
};

} // namespace cpp_mastery::runtime
