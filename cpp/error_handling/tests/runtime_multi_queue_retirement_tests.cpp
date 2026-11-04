#include <cpp_mastery/runtime/MultiQueueGpuRetirementQueue.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::GpuQueueKind;
using cpp_mastery::runtime::MultiQueueGpuRetirementQueue;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::ResourceFactory;
using cpp_mastery::runtime::RuntimeSnapshot;
using cpp_mastery::runtime::SnapshotRuntime;

using Queue = MultiQueueGpuRetirementQueue;
constexpr auto graphics = GpuQueueKind::graphics;
constexpr auto compute = GpuQueueKind::compute;
constexpr auto transfer = GpuQueueKind::transfer;

struct Counts
{
    int created = 0;
    int destroyed = 0;
    int allocations = 0;
};

class CountedBuffer final : public RenderBuffer
{
  public:
    CountedBuffer(Counts& counts, const std::size_t bytes) : counts_{counts}, bytes_{bytes}
    {
        ++counts_.created;
    }

    ~CountedBuffer() override
    {
        ++counts_.destroyed;
    }

    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return bytes_;
    }

  private:
    Counts& counts_;
    std::size_t bytes_;
};

class CountedFactory final : public ResourceFactory
{
  public:
    explicit CountedFactory(Counts& counts) : counts_{counts} {}

    int throw_on_call = -1;

    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(const BufferKind kind,
                                                         const int distance) override
    {
        ++counts_.allocations;
        if (counts_.allocations == throw_on_call)
        {
            throw std::runtime_error{"injected allocation failure"};
        }
        const std::size_t factor = kind == BufferKind::geometry ? 32U : 16U;
        return std::make_unique<CountedBuffer>(counts_,
                                               static_cast<std::size_t>(distance) * factor);
    }

  private:
    Counts& counts_;
};

[[nodiscard]] Queue::SnapshotHandle make_snapshot(CountedFactory& factory, const int distance = 8)
{
    return std::make_shared<const RuntimeSnapshot>(EngineConfig{4, distance}, factory);
}

void serials_are_independent()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    CHECK_EQ(queue.submit(graphics, snap), 1U);
    CHECK_EQ(queue.submit(compute, snap), 1U);
    CHECK_EQ(queue.submit(graphics, snap), 2U);
    CHECK_EQ(queue.submit(transfer, snap), 1U);
    CHECK_EQ(queue.submitted(graphics), 2U);
    CHECK_EQ(queue.submitted(compute), 1U);
    CHECK_EQ(queue.submitted(transfer), 1U);
    CHECK_EQ(queue.completed(graphics), 0U);
    CHECK_EQ(queue.tracked_count(), 1U);
}

void graphics_complete_but_compute_pending()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    (void)queue.submit(compute, snap);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(graphics, 1);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(compute, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void compute_complete_but_graphics_pending()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(compute, snap);
    (void)queue.submit(graphics, snap);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(compute, 1);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(graphics, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void unused_compute_does_not_block_graphics_and_transfer()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    CHECK_EQ(queue.submit(graphics, snap), 1U);
    CHECK_EQ(queue.submit(transfer, snap), 1U);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(graphics, 1);
    queue.complete_through(transfer, 1);
    CHECK_EQ(queue.completed(compute), 0U);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void transfer_completion_is_a_real_dependency()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    (void)queue.submit(transfer, snap);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(graphics, 1);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(transfer, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void all_three_used_queues_must_finish()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    (void)queue.submit(compute, snap);
    (void)queue.submit(transfer, snap);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(graphics, 1);
    queue.complete_through(compute, 1);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(transfer, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void track_last_use_on_each_queue_not_first()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    CHECK_EQ(queue.submit(graphics, snap), 1U);
    CHECK_EQ(queue.submit(graphics, snap), 2U);
    CHECK_EQ(queue.submit(compute, snap), 1U);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(graphics, 1);
    queue.complete_through(compute, 1);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(graphics, 2);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void queue_serial_numbers_are_not_comparable()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    for (int i = 0; i < 12; ++i)
    {
        (void)queue.submit(graphics, snap);
    }
    for (int i = 0; i < 7; ++i)
    {
        (void)queue.submit(compute, snap);
    }
    queue.retire(snap);
    snap.reset();
    queue.complete_through(graphics, 12);
    queue.complete_through(compute, 6);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(compute, 7);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void out_of_order_retirements_are_independent()
{
    Counts counts;
    CountedFactory factory{counts};
    auto a = make_snapshot(factory, 8);
    auto b = make_snapshot(factory, 12);
    Queue queue;
    CHECK_EQ(queue.submit(graphics, a), 1U);
    CHECK_EQ(queue.submit(compute, a), 1U);
    CHECK_EQ(queue.submit(graphics, b), 2U);
    queue.retire(b);
    queue.retire(a);
    a.reset();
    b.reset();
    queue.complete_through(graphics, 2);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(counts.destroyed, 2);
    queue.complete_through(compute, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 4);
}

void no_queue_has_completed()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    (void)queue.submit(compute, snap);
    queue.retire(snap);
    snap.reset();
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(counts.destroyed, 0);
}

void compute_only_retires_without_graphics_progress()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    CHECK_EQ(queue.submit(compute, snap), 1U);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(compute, 1);
    CHECK_EQ(queue.completed(graphics), 0U);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void transfer_only_retires_without_other_progress()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    CHECK_EQ(queue.submit(transfer, snap), 1U);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(transfer, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void never_used_snapshot_retires_immediately()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    queue.retire(snap);
    snap.reset();
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void reader_ownership_extends_after_gpu_completion()
{
    Counts counts;
    CountedFactory factory{counts};
    auto reader = make_snapshot(factory);
    auto second_reader = reader;
    Queue queue;
    (void)queue.submit(graphics, reader);
    (void)queue.submit(transfer, reader);
    queue.retire(reader);
    queue.complete_through(graphics, 1);
    queue.complete_through(transfer, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 0);
    reader.reset();
    CHECK_EQ(counts.destroyed, 0);
    second_reader.reset();
    CHECK_EQ(counts.destroyed, 2);
}

void reject_new_use_after_retirement()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    queue.retire(snap);
    CHECK_THROWS_AS(queue.submit(transfer, snap), std::logic_error);
    CHECK_THROWS_AS(queue.submit(compute, snap), std::logic_error);
    CHECK_EQ(queue.submitted(transfer), 0U);
    CHECK_EQ(queue.submitted(compute), 0U);
}

void sealing_survives_collect_while_reader_exists()
{
    Counts counts;
    CountedFactory factory{counts};
    auto reader = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, reader);
    queue.retire(reader);
    queue.complete_through(graphics, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_THROWS_AS(queue.submit(compute, reader), std::logic_error);
    CHECK_EQ(counts.destroyed, 0);
    reader.reset();
    CHECK_EQ(counts.destroyed, 2);
    CHECK_EQ(queue.collect(), 0U);
}

void reject_duplicate_retirement()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    queue.retire(snap);
    CHECK_THROWS_AS(queue.retire(snap), std::logic_error);
    CHECK_EQ(queue.tracked_count(), 1U);
}

void reject_null_handles()
{
    Queue queue;
    const Queue::SnapshotHandle absent;
    CHECK_THROWS_AS(queue.submit(graphics, absent), std::invalid_argument);
    CHECK_THROWS_AS(queue.retire(absent), std::invalid_argument);
    CHECK_EQ(queue.submitted(graphics), 0U);
    CHECK_EQ(queue.tracked_count(), 0U);
}

void reject_future_completion_separately_for_each_queue()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    CHECK_THROWS_AS(queue.complete_through(compute, 1), std::invalid_argument);
    CHECK_THROWS_AS(queue.complete_through(graphics, 2), std::invalid_argument);
    CHECK_EQ(queue.completed(compute), 0U);
    CHECK_EQ(queue.completed(graphics), 0U);
}

void reject_regressing_completion_without_changing_progress()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    (void)queue.submit(graphics, snap);
    (void)queue.submit(compute, snap);
    queue.complete_through(graphics, 2);
    queue.complete_through(compute, 1);
    CHECK_THROWS_AS(queue.complete_through(graphics, 1), std::invalid_argument);
    CHECK_EQ(queue.completed(graphics), 2U);
    CHECK_EQ(queue.completed(compute), 1U);
}

void reject_invalid_queue_kinds()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    const auto invalid = static_cast<GpuQueueKind>(99);
    CHECK_THROWS_AS(queue.submit(invalid, snap), std::invalid_argument);
    CHECK_THROWS_AS(queue.complete_through(invalid, 0), std::invalid_argument);
    CHECK_THROWS_AS(queue.submitted(invalid), std::invalid_argument);
    CHECK_THROWS_AS(queue.completed(invalid), std::invalid_argument);
    CHECK_EQ(queue.tracked_count(), 0U);
}

void unretired_snapshot_is_never_collected()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    queue.complete_through(graphics, 1);
    snap.reset();
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(counts.destroyed, 0);
}

void collection_is_idempotent()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(compute, snap);
    queue.retire(snap);
    snap.reset();
    queue.complete_through(compute, 1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 2);
}

void publication_failure_does_not_change_tracked_queue_progress()
{
    Counts counts;
    CountedFactory factory{counts};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    auto first = runtime.acquire();
    Queue queue;
    CHECK_EQ(queue.submit(compute, first), 1U);
    factory.throw_on_call = 4;
    CHECK_THROWS_AS(runtime.publish(EngineConfig{8, 24}, factory), std::runtime_error);
    CHECK(runtime.acquire() == first);
    CHECK_EQ(queue.submitted(compute), 1U);
    CHECK_EQ(queue.submitted(graphics), 0U);
    CHECK_EQ(queue.tracked_count(), 1U);
}

void retirement_after_completion_is_immediately_collectible()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snap = make_snapshot(factory);
    Queue queue;
    (void)queue.submit(graphics, snap);
    (void)queue.submit(transfer, snap);
    queue.complete_through(graphics, 1);
    queue.complete_through(transfer, 1);
    queue.retire(snap);
    snap.reset();
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_multi_queue_retirement_suite()
{
    cpp_mastery::test::TestSuite suite{"multi-queue-retirement"};
    suite.add("independent queue serials", serials_are_independent);
    suite.add("graphics complete compute pending", graphics_complete_but_compute_pending);
    suite.add("compute complete graphics pending", compute_complete_but_graphics_pending);
    suite.add("unused compute does not block", unused_compute_does_not_block_graphics_and_transfer);
    suite.add("transfer completion required", transfer_completion_is_a_real_dependency);
    suite.add("all three queues must finish", all_three_used_queues_must_finish);
    suite.add("last use on each queue", track_last_use_on_each_queue_not_first);
    suite.add("independent serials incomparable", queue_serial_numbers_are_not_comparable);
    suite.add("retirement order independent", out_of_order_retirements_are_independent);
    suite.add("no completed queue retains", no_queue_has_completed);
    suite.add("compute-only dependency", compute_only_retires_without_graphics_progress);
    suite.add("transfer-only dependency", transfer_only_retires_without_other_progress);
    suite.add("unused resource immediately eligible", never_used_snapshot_retires_immediately);
    suite.add("CPU readers extend lifetime", reader_ownership_extends_after_gpu_completion);
    suite.add("reject new use after retire", reject_new_use_after_retirement);
    suite.add("seal survives collection", sealing_survives_collect_while_reader_exists);
    suite.add("reject duplicate retirement", reject_duplicate_retirement);
    suite.add("reject null handles", reject_null_handles);
    suite.add("per-queue future completion rejected",
              reject_future_completion_separately_for_each_queue);
    suite.add("completion cannot regress", reject_regressing_completion_without_changing_progress);
    suite.add("unknown queue kinds rejected", reject_invalid_queue_kinds);
    suite.add("unretired snapshot retained", unretired_snapshot_is_never_collected);
    suite.add("collection idempotent", collection_is_idempotent);
    suite.add("publication failure independent",
              publication_failure_does_not_change_tracked_queue_progress);
    suite.add("completion before retirement",
              retirement_after_completion_is_immediately_collectible);
    return suite;
}
