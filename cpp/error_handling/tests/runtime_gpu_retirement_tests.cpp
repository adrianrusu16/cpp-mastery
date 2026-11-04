#include <cpp_mastery/runtime/GpuRetirementQueue.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::GpuRetirementQueue;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::ResourceFactory;
using cpp_mastery::runtime::RuntimeSnapshot;
using cpp_mastery::runtime::SnapshotRuntime;

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
            throw std::runtime_error{"injected staging failure"};
        }
        const std::size_t multiplier = kind == BufferKind::geometry ? 32U : 16U;
        return std::make_unique<CountedBuffer>(counts_,
                                               static_cast<std::size_t>(distance) * multiplier);
    }

  private:
    Counts& counts_;
};

[[nodiscard]] GpuRetirementQueue::SnapshotHandle make_snapshot(CountedFactory& factory,
                                                               const int distance = 8)
{
    return std::make_shared<const RuntimeSnapshot>(EngineConfig{4, distance}, factory);
}

void serials_increase_and_same_snapshot_uses_one_record()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    CHECK_EQ(queue.submit(snapshot), 1U);
    CHECK_EQ(queue.submit(snapshot), 2U);
    CHECK_EQ(queue.submit(snapshot), 3U);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(queue.submitted(), 3U);
    CHECK_EQ(queue.completed(), 0U);
    CHECK_EQ(counts.created, 2);
}

void retire_before_first_completion_keeps_gpu_resource()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    CHECK_EQ(queue.submit(snapshot), 1U);
    queue.retire(snapshot);
    snapshot.reset();

    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void last_use_not_first_use_controls_retirement()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    CHECK_EQ(queue.submit(snapshot), 1U);
    CHECK_EQ(queue.submit(snapshot), 2U);
    queue.retire(snapshot);
    snapshot.reset();

    queue.complete_through(1);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(2);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void queue_owns_snapshot_after_cpu_releases_it()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    CHECK_EQ(queue.submit(snapshot), 1U);
    queue.retire(snapshot);
    snapshot.reset();

    CHECK_EQ(counts.destroyed, 0);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);
}

void gpu_completion_releases_final_owner_exactly_once()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    const auto last = queue.submit(snapshot);
    queue.retire(snapshot);
    snapshot.reset();
    queue.complete_through(last);

    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(queue.tracked_count(), 0U);
    CHECK_EQ(counts.destroyed, 2);
}

void reader_shared_ownership_extends_gpu_completed_lifetime()
{
    Counts counts;
    CountedFactory factory{counts};
    auto reader = make_snapshot(factory);
    auto second_reader = reader;
    GpuRetirementQueue queue;
    const auto id = queue.submit(reader);
    queue.retire(reader);
    queue.complete_through(id);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 0);
    reader.reset();
    CHECK_EQ(counts.destroyed, 0);
    second_reader.reset();
    CHECK_EQ(counts.destroyed, 2);
}

void registration_order_does_not_override_completion()
{
    Counts counts;
    CountedFactory factory{counts};
    auto first = make_snapshot(factory, 8);
    auto second = make_snapshot(factory, 12);
    GpuRetirementQueue queue;
    const auto id_first = queue.submit(first);
    CHECK_EQ(queue.submit(second), 2U);
    queue.retire(second);
    queue.retire(first);
    first.reset();
    second.reset();

    queue.complete_through(id_first);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(counts.destroyed, 2);
    queue.complete_through(2);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 4);
}

void ordered_timeline_39_42_45()
{
    Counts counts;
    CountedFactory factory{counts};
    auto a = make_snapshot(factory, 8);
    auto b = make_snapshot(factory, 12);
    auto c = make_snapshot(factory, 24);
    GpuRetirementQueue queue;

    for (int i = 0; i < 39; ++i)
    {
        (void)queue.submit(a);
    }
    for (int i = 0; i < 3; ++i)
    {
        (void)queue.submit(b);
    }
    for (int i = 0; i < 3; ++i)
    {
        (void)queue.submit(c);
    }
    queue.retire(c);
    queue.retire(a);
    queue.retire(b);
    a.reset();
    b.reset();
    c.reset();

    queue.complete_through(41);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
    CHECK_EQ(queue.tracked_count(), 2U);

    queue.complete_through(42);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 4);
    CHECK_EQ(queue.tracked_count(), 1U);

    queue.complete_through(45);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 6);
}

void never_submitted_snapshot_needs_no_completion()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    queue.retire(snapshot);
    snapshot.reset();
    CHECK_EQ(queue.submitted(), 0U);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void reject_late_gpu_use_after_retirement()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    CHECK_EQ(queue.submit(snapshot), 1U);
    queue.retire(snapshot);
    CHECK_THROWS_AS(queue.submit(snapshot), std::logic_error);
    CHECK_EQ(queue.submitted(), 1U);
    CHECK_EQ(queue.tracked_count(), 1U);
}

void reject_late_use_after_collection_if_reader_still_alive()
{
    Counts counts;
    CountedFactory factory{counts};
    auto reader = make_snapshot(factory);
    GpuRetirementQueue queue;
    const auto id = queue.submit(reader);
    queue.retire(reader);
    queue.complete_through(id);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(queue.tracked_count(), 0U);
    CHECK_EQ(counts.destroyed, 0);

    CHECK_THROWS_AS(queue.submit(reader), std::logic_error);
    CHECK_THROWS_AS(queue.retire(reader), std::logic_error);
    CHECK_EQ(queue.submitted(), id);
    reader.reset();
    CHECK_EQ(counts.destroyed, 2);
}

void reject_duplicate_retirement()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    queue.retire(snapshot);
    CHECK_THROWS_AS(queue.retire(snapshot), std::logic_error);
    CHECK_EQ(queue.tracked_count(), 1U);
}

void reject_null_handles_without_tracking()
{
    GpuRetirementQueue queue;
    const GpuRetirementQueue::SnapshotHandle empty;
    CHECK_THROWS_AS(queue.submit(empty), std::invalid_argument);
    CHECK_THROWS_AS(queue.retire(empty), std::invalid_argument);
    CHECK_EQ(queue.submitted(), 0U);
    CHECK_EQ(queue.tracked_count(), 0U);
}

void reject_unissued_completion()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    CHECK_EQ(queue.submit(snapshot), 1U);
    CHECK_THROWS_AS(queue.complete_through(2), std::invalid_argument);
    CHECK_EQ(queue.completed(), 0U);
}

void reject_regressing_completion()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    CHECK_EQ(queue.submit(snapshot), 1U);
    CHECK_EQ(queue.submit(snapshot), 2U);
    queue.complete_through(2);
    CHECK_THROWS_AS(queue.complete_through(1), std::invalid_argument);
    CHECK_EQ(queue.completed(), 2U);
}

void nonretired_snapshot_is_not_collected()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    const auto id = queue.submit(snapshot);
    snapshot.reset();
    queue.complete_through(id);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);
    CHECK_EQ(queue.tracked_count(), 1U);
}

void collection_is_idempotent()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    queue.retire(snapshot);
    snapshot.reset();
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 2);
}

void publisher_replacement_and_gpu_retirement_are_separate()
{
    Counts counts;
    CountedFactory factory{counts};
    SnapshotRuntime publisher{EngineConfig{4, 8}, factory};
    auto old = publisher.acquire();
    GpuRetirementQueue queue;
    const auto id = queue.submit(old);

    publisher.publish(EngineConfig{8, 24}, factory);
    queue.retire(old);
    old.reset();
    CHECK_EQ(publisher.acquire()->settings().render_distance, 24);
    CHECK_EQ(counts.destroyed, 0);
    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);

    queue.complete_through(id);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void failed_publication_does_not_destroy_tracked_old_gpu_use()
{
    Counts counts;
    CountedFactory factory{counts};
    SnapshotRuntime publisher{EngineConfig{4, 8}, factory};
    auto old = publisher.acquire();
    GpuRetirementQueue queue;
    const auto id = queue.submit(old);
    factory.throw_on_call = 4;
    CHECK_THROWS_AS(publisher.publish(EngineConfig{8, 24}, factory), std::runtime_error);
    CHECK(publisher.acquire() == old);
    CHECK_EQ(queue.submitted(), id);
    CHECK_EQ(queue.tracked_count(), 1U);
    CHECK_EQ(counts.destroyed, 1);
}

void retire_even_if_completion_arrived_earlier()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    GpuRetirementQueue queue;
    const auto id = queue.submit(snapshot);
    queue.complete_through(id);
    CHECK_EQ(queue.collect(), 0U);
    queue.retire(snapshot);
    snapshot.reset();
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
}

void old_cpu_handle_keeps_snapshot_after_queue_collection()
{
    Counts counts;
    CountedFactory factory{counts};
    auto snapshot = make_snapshot(factory);
    auto reader = snapshot;
    GpuRetirementQueue queue;
    const auto id = queue.submit(snapshot);
    queue.retire(snapshot);
    snapshot.reset();
    queue.complete_through(id);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 0);
    REQUIRE(reader->geometry_buffer() != nullptr);
    CHECK_EQ(reader->geometry_buffer()->size_bytes(), 8U * 32U);
    reader.reset();
    CHECK_EQ(counts.destroyed, 2);
}

void all_retired_snapshots_wait_for_last_complete()
{
    Counts counts;
    CountedFactory factory{counts};
    GpuRetirementQueue queue;
    auto early = make_snapshot(factory, 8);
    auto late = make_snapshot(factory, 24);
    CHECK_EQ(queue.submit(early), 1U);
    CHECK_EQ(queue.submit(late), 2U);
    queue.retire(early);
    queue.retire(late);
    early.reset();
    late.reset();

    CHECK_EQ(queue.collect(), 0U);
    CHECK_EQ(counts.destroyed, 0);
    queue.complete_through(1);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 2);
    queue.complete_through(2);
    CHECK_EQ(queue.collect(), 1U);
    CHECK_EQ(counts.destroyed, 4);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_gpu_retirement_suite()
{
    cpp_mastery::test::TestSuite suite{"gpu-retirement"};
    suite.add("submission serials and record uniqueness",
              serials_increase_and_same_snapshot_uses_one_record);
    suite.add("incomplete submission keeps resources",
              retire_before_first_completion_keeps_gpu_resource);
    suite.add("last use controls retirement", last_use_not_first_use_controls_retirement);
    suite.add("GPU queue holds owner after CPU releases it",
              queue_owns_snapshot_after_cpu_releases_it);
    suite.add("completion releases once", gpu_completion_releases_final_owner_exactly_once);
    suite.add("CPU readers extend lifetime",
              reader_shared_ownership_extends_gpu_completed_lifetime);
    suite.add("out-of-order retirement", registration_order_does_not_override_completion);
    suite.add("ordered timeline 39 42 45", ordered_timeline_39_42_45);
    suite.add("unused snapshot retires immediately", never_submitted_snapshot_needs_no_completion);
    suite.add("reject late use", reject_late_gpu_use_after_retirement);
    suite.add("seal survives collection while CPU reader exists",
              reject_late_use_after_collection_if_reader_still_alive);
    suite.add("reject duplicate retirement", reject_duplicate_retirement);
    suite.add("reject null handles", reject_null_handles_without_tracking);
    suite.add("reject completion beyond submissions", reject_unissued_completion);
    suite.add("reject regressing completion", reject_regressing_completion);
    suite.add("nonretired is never collected", nonretired_snapshot_is_not_collected);
    suite.add("collection is idempotent", collection_is_idempotent);
    suite.add("publication and GPU retirement independent",
              publisher_replacement_and_gpu_retirement_are_separate);
    suite.add("failed publication preserves old GPU use",
              failed_publication_does_not_destroy_tracked_old_gpu_use);
    suite.add("completion before retirement", retire_even_if_completion_arrived_earlier);
    suite.add("reader handles survive collection",
              old_cpu_handle_keeps_snapshot_after_queue_collection);
    suite.add("multiple pending GPU uses", all_retired_snapshots_wait_for_last_complete);
    return suite;
}
