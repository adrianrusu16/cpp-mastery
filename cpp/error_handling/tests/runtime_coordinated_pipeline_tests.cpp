#include <cpp_mastery/runtime/CoordinatedRuntime.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::CoordinatedRuntime;
using cpp_mastery::runtime::CrossQueueHandoff;
using cpp_mastery::runtime::GpuQueueKind;
using cpp_mastery::runtime::HandoffSharing;
using cpp_mastery::runtime::HandoffWaitStage;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::ResourceFactory;

static_assert(!std::is_copy_constructible_v<CoordinatedRuntime::Admission>);
static_assert(std::is_nothrow_move_constructible_v<CoordinatedRuntime::Admission>);

struct Counters
{
    std::atomic<int> created{0};
    std::atomic<int> destroyed{0};
    int calls = 0;
};

class TrackedBuffer final : public RenderBuffer
{
  public:
    TrackedBuffer(Counters& counts, const int distance) : counts_{counts}, distance_{distance}
    {
        counts_.created.fetch_add(1);
    }

    ~TrackedBuffer() override
    {
        counts_.destroyed.fetch_add(1);
    }

    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return static_cast<std::size_t>(distance_) * 32U;
    }

  private:
    Counters& counts_;
    int distance_;
};

class Factory final : public ResourceFactory
{
  public:
    explicit Factory(Counters& counts) : counts_{counts} {}

    int throw_on_call = -1;

    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(BufferKind, int distance) override
    {
        ++counts_.calls;
        if (counts_.calls == throw_on_call)
        {
            throw std::runtime_error{"injected preparation failure"};
        }
        return std::make_unique<TrackedBuffer>(counts_, distance);
    }

  private:
    Counters& counts_;
};

void check_settings(const CoordinatedRuntime& runtime, int workers, int distance)
{
    auto snap = runtime.acquire();
    REQUIRE(snap != nullptr);
    CHECK_EQ(snap->settings().worker_threads, workers);
    CHECK_EQ(snap->settings().render_distance, distance);
}

[[nodiscard]] std::optional<std::uint64_t> try_submit(CoordinatedRuntime::Admission& ticket,
                                                      const GpuQueueKind queue)
{
    try
    {
        return ticket.submit(queue);
    }
    catch (const std::logic_error&)
    {
        return std::nullopt;
    }
}

void initial_snapshot_is_complete()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    check_settings(runtime, 4, 8);
    CHECK_EQ(counts.created.load(), 2);
    CHECK_EQ(runtime.pending_admissions(), 0U);
}

void publication_replaces_cpu_snapshot()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto old = runtime.acquire();
    runtime.publish(EngineConfig{8, 24}, factory);
    check_settings(runtime, 8, 24);
    CHECK(old != runtime.acquire());
    CHECK_EQ(old->settings().render_distance, 8);
}

void pending_ticket_survives_publication()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    auto old = ticket.snapshot();
    runtime.publish(EngineConfig{16, 24}, factory);
    CHECK_EQ(runtime.collect(), 0U);
    CHECK_EQ(runtime.retired_generations(), 1U);
    REQUIRE(ticket.snapshot() == old);
    const auto accepted = try_submit(ticket, GpuQueueKind::graphics);
    CHECK(accepted.has_value());
    if (!accepted)
    {
        return;
    }
    CHECK_EQ(*accepted, 1U);
    CHECK_EQ(runtime.pending_admissions(), 0U);
    CHECK_EQ(runtime.collect(), 0U);
    runtime.complete_through(GpuQueueKind::graphics, 1);
    CHECK_EQ(runtime.collect(), 1U);
}

void old_ticket_remains_admitted_after_new_publication()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto old_ticket = runtime.admit();
    runtime.publish(EngineConfig{8, 12}, factory);
    CHECK_EQ(runtime.collect(), 0U);
    auto next_ticket = runtime.admit();
    CHECK_EQ(next_ticket.snapshot()->settings().render_distance, 12);
    CHECK_EQ(old_ticket.snapshot()->settings().render_distance, 8);
    const auto accepted = try_submit(old_ticket, GpuQueueKind::transfer);
    CHECK(accepted.has_value());
    if (!accepted)
    {
        return;
    }
    CHECK_EQ(*accepted, 1U);
    CHECK_EQ(next_ticket.submit(GpuQueueKind::graphics), 1U);
    CHECK_EQ(runtime.collect(), 0U);
    runtime.complete_through(GpuQueueKind::transfer, 1);
    CHECK_EQ(runtime.collect(), 1U);
}

void cancelled_old_ticket_allows_retirement()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    runtime.publish(EngineConfig{8, 12}, factory);
    CHECK_EQ(runtime.collect(), 0U);
    ticket.cancel();
    CHECK_EQ(runtime.collect(), 1U);
    CHECK_EQ(runtime.submitted(GpuQueueKind::graphics), 0U);
}

void destructor_cancels_pending_ticket()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    {
        auto ticket = runtime.admit();
        runtime.publish(EngineConfig{8, 12}, factory);
        CHECK_EQ(runtime.collect(), 0U);
    }
    CHECK_EQ(runtime.pending_admissions(), 0U);
    CHECK_EQ(runtime.collect(), 1U);
}

void multiple_tickets_require_last_resolution()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto first = runtime.admit();
    auto second = runtime.admit();
    runtime.publish(EngineConfig{8, 12}, factory);
    CHECK_EQ(runtime.collect(), 0U);
    first.cancel();
    CHECK_EQ(runtime.collect(), 0U);
    const auto accepted = try_submit(second, GpuQueueKind::compute);
    CHECK(accepted.has_value());
    if (!accepted)
    {
        return;
    }
    CHECK_EQ(*accepted, 1U);
    CHECK_EQ(runtime.collect(), 0U);
    runtime.complete_through(GpuQueueKind::compute, 1);
    CHECK_EQ(runtime.collect(), 1U);
}

void independent_queue_completion_blocks_retirement()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto a = runtime.admit();
    auto b = runtime.admit();
    runtime.publish(EngineConfig{8, 12}, factory);
    CHECK_EQ(runtime.collect(), 0U);
    const auto accepted = try_submit(a, GpuQueueKind::graphics);
    CHECK(accepted.has_value());
    if (!accepted)
    {
        return;
    }
    CHECK_EQ(*accepted, 1U);
    const auto transfer = try_submit(b, GpuQueueKind::transfer);
    CHECK(transfer.has_value());
    if (!transfer)
    {
        return;
    }
    CHECK_EQ(*transfer, 1U);
    runtime.complete_through(GpuQueueKind::graphics, 1);
    CHECK_EQ(runtime.collect(), 0U);
    runtime.complete_through(GpuQueueKind::transfer, 1);
    CHECK_EQ(runtime.collect(), 1U);
}

void unused_compute_queue_does_not_block()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    runtime.publish(EngineConfig{8, 12}, factory);
    CHECK_EQ(runtime.collect(), 0U);
    const auto accepted = try_submit(ticket, GpuQueueKind::graphics);
    CHECK(accepted.has_value());
    if (!accepted)
    {
        return;
    }
    CHECK_EQ(*accepted, 1U);
    runtime.complete_through(GpuQueueKind::graphics, 1);
    CHECK_EQ(runtime.collect(), 1U);
}

void cpu_reader_retains_resources_after_gpu_completion()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto cpu_reader = runtime.acquire();
    auto ticket = runtime.admit();
    runtime.publish(EngineConfig{8, 12}, factory);
    CHECK_EQ(runtime.collect(), 0U);
    const auto accepted = try_submit(ticket, GpuQueueKind::graphics);
    CHECK(accepted.has_value());
    if (!accepted)
    {
        return;
    }
    CHECK_EQ(*accepted, 1U);
    runtime.complete_through(GpuQueueKind::graphics, 1);
    CHECK_EQ(runtime.collect(), 1U);
    CHECK_EQ(counts.destroyed.load(), 0);
    cpu_reader.reset();
    CHECK_EQ(counts.destroyed.load(), 2);
}

void failed_publication_preserves_current_and_ticket()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    auto before = runtime.acquire();
    factory.throw_on_call = 4;
    CHECK_THROWS_AS(runtime.publish(EngineConfig{16, 24}, factory), std::runtime_error);
    CHECK(runtime.acquire() == before);
    CHECK_EQ(runtime.retired_generations(), 0U);
    CHECK_EQ(ticket.submit(GpuQueueKind::graphics), 1U);
}

void failed_publication_without_ticket_is_atomic()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto before = runtime.acquire();
    factory.throw_on_call = 3;
    CHECK_THROWS_AS(runtime.publish(EngineConfig{16, 24}, factory), std::runtime_error);
    CHECK(runtime.acquire() == before);
    CHECK_EQ(runtime.retired_generations(), 0U);
}

void ticket_does_not_issue_gpu_serial_until_submission()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    CHECK_EQ(runtime.submitted(GpuQueueKind::graphics), 0U);
    CHECK_EQ(runtime.submitted(GpuQueueKind::compute), 0U);
    ticket.cancel();
    CHECK_EQ(runtime.submitted(GpuQueueKind::graphics), 0U);
}

void invalid_queue_does_not_consume_ticket()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    CHECK_THROWS_AS(ticket.submit(static_cast<GpuQueueKind>(99)), std::invalid_argument);
    CHECK_EQ(runtime.pending_admissions(), 1U);
    CHECK_EQ(ticket.submit(GpuQueueKind::graphics), 1U);
}

void move_transfers_reservation_ownership()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto first = runtime.admit();
    auto second = std::move(first);
    CHECK(first.snapshot() == nullptr);
    CHECK_EQ(runtime.pending_admissions(), 1U);
    second.cancel();
    CHECK_EQ(runtime.pending_admissions(), 0U);
}

void consumed_ticket_rejects_duplicate_use()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    CHECK_EQ(ticket.submit(GpuQueueKind::graphics), 1U);
    CHECK_THROWS_AS(ticket.submit(GpuQueueKind::graphics), std::logic_error);
    CHECK_EQ(runtime.submitted(GpuQueueKind::graphics), 1U);
}

void cancel_is_idempotent()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    ticket.cancel();
    ticket.cancel();
    CHECK_EQ(runtime.pending_admissions(), 0U);
    CHECK_THROWS_AS(ticket.submit(GpuQueueKind::graphics), std::logic_error);
}

void unused_generation_retires_immediately()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    runtime.publish(EngineConfig{8, 12}, factory);
    CHECK_EQ(runtime.collect(), 1U);
    CHECK_EQ(runtime.retired_generations(), 0U);
    CHECK_EQ(counts.destroyed.load(), 2);
}

void handoff_blocks_graphics_admission_until_ready()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    CrossQueueHandoff handoff{HandoffSharing::exclusive, 2, 3};
    const auto upload = handoff.transfer_write();
    handoff.signal_transfer(upload);
    handoff.wait_graphics(upload, HandoffWaitStage::all_commands);
    CHECK_THROWS_AS(ticket.submit_graphics_after(handoff), std::logic_error);
    CHECK_EQ(runtime.pending_admissions(), 1U);
    handoff.release_to_graphics(upload);
    handoff.acquire_on_graphics(upload);
    CHECK_EQ(ticket.submit_graphics_after(handoff), 1U);
}

void concurrent_admission_survives_publish_and_collect()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto ticket = runtime.admit();
    std::promise<void> started;
    std::promise<void> release;
    auto proceed = release.get_future().share();
    std::promise<std::uint64_t> result;
    auto received = result.get_future();

    std::jthread reader{[ticket = std::move(ticket), &started, &result, proceed]() mutable
                        {
                            started.set_value();
                            proceed.wait();
                            try
                            {
                                result.set_value(ticket.submit(GpuQueueKind::graphics));
                            }
                            catch (...)
                            {
                                result.set_exception(std::current_exception());
                            }
                        }};
    started.get_future().wait();
    runtime.publish(EngineConfig{16, 24}, factory);
    const auto collected_before = runtime.collect();
    release.set_value();
    std::optional<std::uint64_t> serial;
    try
    {
        serial = received.get();
    }
    catch (const std::logic_error&)
    {
        CHECK(false);
    }
    CHECK_EQ(collected_before, 0U);
    if (!serial)
    {
        return;
    }
    CHECK_EQ(*serial, 1U);
    runtime.complete_through(GpuQueueKind::graphics, 1);
    CHECK_EQ(runtime.collect(), 1U);
}

void ticket_can_outlive_coordinator_and_cancel()
{
    Counters counts;
    Factory factory{counts};
    auto runtime = std::make_unique<CoordinatedRuntime>(EngineConfig{4, 8}, factory);
    auto ticket = runtime->admit();
    runtime.reset();
    REQUIRE(ticket.snapshot() != nullptr);
    ticket.cancel();
    CHECK_EQ(counts.destroyed.load(), 2);
}

void consecutive_publications_do_not_bypass_pending_generation()
{
    Counters counts;
    Factory factory{counts};
    CoordinatedRuntime runtime{EngineConfig{4, 8}, factory};
    auto first = runtime.admit();
    runtime.publish(EngineConfig{8, 12}, factory);
    runtime.publish(EngineConfig{16, 24}, factory);
    CHECK_EQ(runtime.collect(), 1U);
    CHECK_EQ(runtime.retired_generations(), 1U);
    const auto accepted = try_submit(first, GpuQueueKind::graphics);
    CHECK(accepted.has_value());
    if (!accepted)
    {
        return;
    }
    CHECK_EQ(*accepted, 1U);
    runtime.complete_through(GpuQueueKind::graphics, 1);
    CHECK_EQ(runtime.collect(), 1U);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_coordinated_pipeline_suite()
{
    cpp_mastery::test::TestSuite suite{"coordinated-pipeline"};
    suite.add("initial snapshot complete", initial_snapshot_is_complete);
    suite.add("publication replaces CPU snapshot", publication_replaces_cpu_snapshot);
    suite.add("pending admission survives publication", pending_ticket_survives_publication);
    suite.add("old ticket after new publication",
              old_ticket_remains_admitted_after_new_publication);
    suite.add("cancellation permits retirement", cancelled_old_ticket_allows_retirement);
    suite.add("destructor cancels admission", destructor_cancels_pending_ticket);
    suite.add("multiple tickets require final resolution",
              multiple_tickets_require_last_resolution);
    suite.add("multi-queue retirement after admission",
              independent_queue_completion_blocks_retirement);
    suite.add("unused compute does not block", unused_compute_queue_does_not_block);
    suite.add("CPU reader extends GPU-complete lifetime",
              cpu_reader_retains_resources_after_gpu_completion);
    suite.add("failed publish retains pending ticket",
              failed_publication_preserves_current_and_ticket);
    suite.add("failed publish preserves snapshot", failed_publication_without_ticket_is_atomic);
    suite.add("admission does not issue GPU serial",
              ticket_does_not_issue_gpu_serial_until_submission);
    suite.add("bad queue keeps reservation", invalid_queue_does_not_consume_ticket);
    suite.add("ticket move transfers ownership", move_transfers_reservation_ownership);
    suite.add("duplicate use rejected", consumed_ticket_rejects_duplicate_use);
    suite.add("cancel is idempotent", cancel_is_idempotent);
    suite.add("unused version retires immediately", unused_generation_retires_immediately);
    suite.add("handoff gates graphics submission", handoff_blocks_graphics_admission_until_ready);
    suite.add("concurrent admitted render survives publish",
              concurrent_admission_survives_publish_and_collect);
    suite.add("ticket outlives coordinator", ticket_can_outlive_coordinator_and_cancel);
    suite.add("multiple publications honor old tickets",
              consecutive_publications_do_not_bypass_pending_generation);
    return suite;
}
