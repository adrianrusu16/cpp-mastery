#include <cpp_mastery/runtime/CrossQueueHandoff.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstdint>
#include <stdexcept>

namespace
{

using cpp_mastery::runtime::CrossQueueHandoff;
using cpp_mastery::runtime::HandoffSharing;
using cpp_mastery::runtime::HandoffWaitStage;

using Handoff = CrossQueueHandoff;
constexpr auto vertex = HandoffWaitStage::vertex_input;
constexpr auto fragment = HandoffWaitStage::fragment_shader;
constexpr auto all = HandoffWaitStage::all_commands;

[[nodiscard]] Handoff exclusive()
{
    return Handoff{HandoffSharing::exclusive, 2U, 7U};
}

[[nodiscard]] Handoff concurrent()
{
    return Handoff{HandoffSharing::concurrent, 2U, 7U};
}

[[nodiscard]] Handoff same_family()
{
    return Handoff{HandoffSharing::exclusive, 2U, 2U};
}

void signal_and_wait(Handoff& handoff, const Handoff::SubmissionId value,
                     const HandoffWaitStage stage = vertex)
{
    handoff.signal_transfer(value);
    handoff.wait_graphics(value, stage);
}

void initial_state_disallows_read()
{
    auto handoff = concurrent();
    REQUIRE_FALSE(handoff.graphics_read_ready());
    CHECK_THROWS_AS(handoff.graphics_read(), std::logic_error);
    CHECK_EQ(handoff.reads(), 0U);
}

void upload_without_signal_is_not_ready()
{
    auto handoff = concurrent();
    CHECK_EQ(handoff.transfer_write(), 1U);
    REQUIRE_FALSE(handoff.graphics_read_ready());
    CHECK_THROWS_AS(handoff.graphics_read(), std::logic_error);
}

void signal_without_wait_is_not_ready()
{
    auto handoff = concurrent();
    const auto version = handoff.transfer_write();
    handoff.signal_transfer(version);
    REQUIRE_FALSE(handoff.graphics_read_ready());
}

void concurrent_sharing_uses_semaphore_dependency()
{
    auto handoff = concurrent();
    REQUIRE_FALSE(handoff.ownership_transfer_required());
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version);
    REQUIRE(handoff.graphics_read_ready());
    CHECK_EQ(handoff.graphics_read(), version);
    CHECK_EQ(handoff.reads(), 1U);
}

void fragment_wait_does_not_cover_vertex_read()
{
    auto handoff = concurrent();
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version, fragment);
    CHECK_FALSE(handoff.graphics_read_ready());
}

void exclusive_cross_family_missing_release_and_acquire()
{
    auto handoff = exclusive();
    REQUIRE(handoff.ownership_transfer_required());
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version, all);
    CHECK_FALSE(handoff.graphics_read_ready());
}

void released_exclusive_buffer_requires_acquire()
{
    auto handoff = exclusive();
    const auto version = handoff.transfer_write();
    handoff.release_to_graphics(version);
    signal_and_wait(handoff, version, all);
    CHECK_FALSE(handoff.graphics_read_ready());
}

void exclusive_handoff_requires_matching_release_and_acquire()
{
    auto handoff = exclusive();
    const auto version = handoff.transfer_write();
    handoff.release_to_graphics(version);
    signal_and_wait(handoff, version, all);
    handoff.acquire_on_graphics(version);
    REQUIRE(handoff.graphics_read_ready());
    CHECK_EQ(handoff.graphics_read(), version);
}

void exclusive_handoff_needs_all_commands_scope()
{
    auto handoff = exclusive();
    const auto version = handoff.transfer_write();
    handoff.release_to_graphics(version);
    signal_and_wait(handoff, version, vertex);
    CHECK_THROWS_AS(handoff.acquire_on_graphics(version), std::logic_error);
    CHECK_FALSE(handoff.graphics_read_ready());
}

void all_commands_scope_covers_concurrent_vertex_read()
{
    auto handoff = concurrent();
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version, all);
    REQUIRE(handoff.graphics_read_ready());
}

void same_family_exclusive_needs_no_ownership_transfer()
{
    auto handoff = same_family();
    REQUIRE_FALSE(handoff.ownership_transfer_required());
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version, vertex);
    REQUIRE(handoff.graphics_read_ready());
}

void second_write_invalidates_old_wait()
{
    auto handoff = concurrent();
    const auto first = handoff.transfer_write();
    signal_and_wait(handoff, first);
    REQUIRE(handoff.graphics_read_ready());
    CHECK_EQ(handoff.transfer_write(), first + 1U);
    CHECK_FALSE(handoff.graphics_read_ready());
}

void second_write_invalidates_old_ownership()
{
    auto handoff = exclusive();
    const auto first = handoff.transfer_write();
    handoff.release_to_graphics(first);
    signal_and_wait(handoff, first, all);
    handoff.acquire_on_graphics(first);
    REQUIRE(handoff.graphics_read_ready());
    const auto second = handoff.transfer_write();
    signal_and_wait(handoff, second, all);
    CHECK_FALSE(handoff.graphics_read_ready());
}

void fresh_release_without_fresh_acquire_stays_blocked()
{
    auto handoff = exclusive();
    const auto first = handoff.transfer_write();
    handoff.release_to_graphics(first);
    signal_and_wait(handoff, first, all);
    handoff.acquire_on_graphics(first);
    const auto second = handoff.transfer_write();
    handoff.release_to_graphics(second);
    signal_and_wait(handoff, second, all);
    CHECK_FALSE(handoff.graphics_read_ready());
}

void unsafe_read_throws_instead_of_pretending_safe()
{
    auto handoff = exclusive();
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version, all);
    CHECK_THROWS_AS(handoff.graphics_read(), std::logic_error);
    CHECK_EQ(handoff.reads(), 0U);
}

void wrong_wait_stage_blocks_read()
{
    auto handoff = concurrent();
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version, fragment);
    CHECK_THROWS_AS(handoff.graphics_read(), std::logic_error);
}

void signal_for_future_write_rejected()
{
    auto handoff = concurrent();
    CHECK_EQ(handoff.transfer_write(), 1U);
    CHECK_THROWS_AS(handoff.signal_transfer(2U), std::invalid_argument);
    REQUIRE_FALSE(handoff.graphics_read_ready());
}

void wait_without_signal_rejected()
{
    auto handoff = concurrent();
    CHECK_EQ(handoff.transfer_write(), 1U);
    CHECK_THROWS_AS(handoff.wait_graphics(1U, vertex), std::invalid_argument);
}

void zero_signal_rejected()
{
    auto handoff = concurrent();
    CHECK_EQ(handoff.transfer_write(), 1U);
    CHECK_THROWS_AS(handoff.signal_transfer(0U), std::invalid_argument);
}

void zero_wait_rejected()
{
    auto handoff = concurrent();
    CHECK_EQ(handoff.transfer_write(), 1U);
    handoff.signal_transfer(1U);
    CHECK_THROWS_AS(handoff.wait_graphics(0U, vertex), std::invalid_argument);
}

void duplicate_release_rejected()
{
    auto handoff = exclusive();
    const auto version = handoff.transfer_write();
    handoff.release_to_graphics(version);
    CHECK_THROWS_AS(handoff.release_to_graphics(version), std::logic_error);
}

void duplicate_acquire_rejected()
{
    auto handoff = exclusive();
    const auto version = handoff.transfer_write();
    handoff.release_to_graphics(version);
    signal_and_wait(handoff, version, all);
    handoff.acquire_on_graphics(version);
    CHECK_THROWS_AS(handoff.acquire_on_graphics(version), std::logic_error);
}

void release_for_outdated_write_rejected()
{
    auto handoff = exclusive();
    CHECK_EQ(handoff.transfer_write(), 1U);
    CHECK_EQ(handoff.transfer_write(), 2U);
    CHECK_THROWS_AS(handoff.release_to_graphics(1U), std::invalid_argument);
}

void acquire_for_outdated_write_rejected()
{
    auto handoff = exclusive();
    const auto first = handoff.transfer_write();
    handoff.release_to_graphics(first);
    signal_and_wait(handoff, first, all);
    const auto second = handoff.transfer_write();
    CHECK_EQ(second, 2U);
    CHECK_THROWS_AS(handoff.acquire_on_graphics(first), std::logic_error);
}

void acquire_without_wait_rejected()
{
    auto handoff = exclusive();
    const auto version = handoff.transfer_write();
    handoff.release_to_graphics(version);
    handoff.signal_transfer(version);
    CHECK_THROWS_AS(handoff.acquire_on_graphics(version), std::logic_error);
}

void acquire_without_release_rejected()
{
    auto handoff = exclusive();
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version, all);
    CHECK_THROWS_AS(handoff.acquire_on_graphics(version), std::logic_error);
}

void same_family_release_is_unnecessary()
{
    auto handoff = same_family();
    const auto version = handoff.transfer_write();
    CHECK_THROWS_AS(handoff.release_to_graphics(version), std::logic_error);
}

void concurrent_release_is_unnecessary()
{
    auto handoff = concurrent();
    const auto version = handoff.transfer_write();
    CHECK_THROWS_AS(handoff.release_to_graphics(version), std::logic_error);
}

void invalid_wait_stage_rejected()
{
    auto handoff = concurrent();
    const auto version = handoff.transfer_write();
    handoff.signal_transfer(version);
    CHECK_THROWS_AS(handoff.wait_graphics(version, static_cast<HandoffWaitStage>(99)),
                    std::invalid_argument);
}

void batched_transfer_signal_can_cover_prior_writes()
{
    auto handoff = concurrent();
    CHECK_EQ(handoff.transfer_write(), 1U);
    CHECK_EQ(handoff.transfer_write(), 2U);
    CHECK_EQ(handoff.transfer_write(), 3U);
    signal_and_wait(handoff, 3U);
    REQUIRE(handoff.graphics_read_ready());
    CHECK_EQ(handoff.graphics_read(), 3U);
}

void old_signal_cannot_cover_newest_write()
{
    auto handoff = concurrent();
    const auto first = handoff.transfer_write();
    handoff.signal_transfer(first);
    CHECK_EQ(handoff.transfer_write(), 2U);
    handoff.wait_graphics(first, all);
    CHECK_FALSE(handoff.graphics_read_ready());
}

void repeated_synchronized_reads_are_safe()
{
    auto handoff = concurrent();
    const auto version = handoff.transfer_write();
    signal_and_wait(handoff, version);
    CHECK_EQ(handoff.graphics_read(), version);
    CHECK_EQ(handoff.graphics_read(), version);
    CHECK_EQ(handoff.reads(), 2U);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_cross_queue_handoff_suite()
{
    cpp_mastery::test::TestSuite suite{"cross-queue-handoff"};
    suite.add("initial state disallows read", initial_state_disallows_read);
    suite.add("upload without signal is not ready", upload_without_signal_is_not_ready);
    suite.add("signal without wait is not ready", signal_without_wait_is_not_ready);
    suite.add("concurrent sharing uses semaphore dependency",
              concurrent_sharing_uses_semaphore_dependency);
    suite.add("fragment wait does not cover vertex read", fragment_wait_does_not_cover_vertex_read);
    suite.add("exclusive missing both ownership barriers",
              exclusive_cross_family_missing_release_and_acquire);
    suite.add("exclusive release without acquire", released_exclusive_buffer_requires_acquire);
    suite.add("matching release and acquire succeeds",
              exclusive_handoff_requires_matching_release_and_acquire);
    suite.add("exclusive requires all commands scope", exclusive_handoff_needs_all_commands_scope);
    suite.add("all commands covers concurrent vertex read",
              all_commands_scope_covers_concurrent_vertex_read);
    suite.add("exclusive same family avoids transfer",
              same_family_exclusive_needs_no_ownership_transfer);
    suite.add("new write invalidates old wait", second_write_invalidates_old_wait);
    suite.add("new write invalidates old ownership", second_write_invalidates_old_ownership);
    suite.add("fresh release still needs fresh acquire",
              fresh_release_without_fresh_acquire_stays_blocked);
    suite.add("unsafe graphics read throws", unsafe_read_throws_instead_of_pretending_safe);
    suite.add("wrong wait stage blocks read", wrong_wait_stage_blocks_read);
    suite.add("reject future signal", signal_for_future_write_rejected);
    suite.add("reject wait before signal", wait_without_signal_rejected);
    suite.add("reject zero signal", zero_signal_rejected);
    suite.add("reject zero wait", zero_wait_rejected);
    suite.add("reject duplicate release", duplicate_release_rejected);
    suite.add("reject duplicate acquire", duplicate_acquire_rejected);
    suite.add("reject stale release", release_for_outdated_write_rejected);
    suite.add("reject stale acquire", acquire_for_outdated_write_rejected);
    suite.add("reject acquire without wait", acquire_without_wait_rejected);
    suite.add("reject acquire without release", acquire_without_release_rejected);
    suite.add("same family release unnecessary", same_family_release_is_unnecessary);
    suite.add("concurrent sharing release unnecessary", concurrent_release_is_unnecessary);
    suite.add("reject unsupported wait stage", invalid_wait_stage_rejected);
    suite.add("batched write signal covers latest", batched_transfer_signal_can_cover_prior_writes);
    suite.add("old signal does not cover new write", old_signal_cannot_cover_newest_write);
    suite.add("repeated synchronized reads", repeated_synchronized_reads_are_safe);
    return suite;
}
