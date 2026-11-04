#include <cpp_mastery/runtime/SnapshotRuntime.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <atomic>
#include <cstddef>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::ResourceFactory;
using cpp_mastery::runtime::RuntimeSnapshot;
using cpp_mastery::runtime::SnapshotRuntime;

using SnapshotHandle = std::shared_ptr<const RuntimeSnapshot>;
static_assert(
    std::is_same_v<decltype(std::declval<const SnapshotRuntime&>().acquire()), SnapshotHandle>);
static_assert(std::is_const_v<SnapshotHandle::element_type>);
static_assert(!noexcept(std::declval<SnapshotRuntime&>().publish(
    std::declval<const EngineConfig&>(), std::declval<ResourceFactory&>())));

struct Ledger
{
    std::atomic<int> created{0};
    std::atomic<int> destroyed{0};
    std::atomic<int> live{0};
};

class TrackedBuffer final : public RenderBuffer
{
  public:
    TrackedBuffer(std::shared_ptr<Ledger> ledger, const std::size_t bytes)
        : ledger_{std::move(ledger)}, bytes_{bytes}
    {
        ledger_->created.fetch_add(1);
        ledger_->live.fetch_add(1);
    }

    ~TrackedBuffer() override
    {
        ledger_->destroyed.fetch_add(1);
        ledger_->live.fetch_sub(1);
    }

    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return bytes_;
    }

  private:
    std::shared_ptr<Ledger> ledger_;
    std::size_t bytes_;
};

class TrackedFactory final : public ResourceFactory
{
  public:
    explicit TrackedFactory(std::shared_ptr<Ledger> ledger) : ledger_{std::move(ledger)} {}

    int throw_on_call = -1;
    int null_on_call = -1;
    int calls = 0;

    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(const BufferKind kind,
                                                         const int render_distance) override
    {
        ++calls;
        if (calls == throw_on_call)
        {
            throw std::runtime_error{"injected snapshot allocation failure"};
        }
        if (calls == null_on_call)
        {
            return nullptr;
        }
        const std::size_t multiplier = kind == BufferKind::geometry ? 256U : 128U;
        return std::make_unique<TrackedBuffer>(ledger_, static_cast<std::size_t>(render_distance) *
                                                            multiplier);
    }

  private:
    std::shared_ptr<Ledger> ledger_;
};

// Block only the lighting allocation of the new snapshot. Unlike a sleep,
// the latch proves that the writer is between its two resource allocations.
class BlockingFactory final : public ResourceFactory
{
  public:
    explicit BlockingFactory(ResourceFactory& underlying) : underlying_{underlying} {}

    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(const BufferKind kind,
                                                         const int render_distance) override
    {
        if (kind == BufferKind::lighting)
        {
            entered_.count_down();
            release_.wait();
        }
        return underlying_.allocate(kind, render_distance);
    }

    void wait_until_blocked() const
    {
        entered_.wait();
    }

    void unblock()
    {
        release_.count_down();
    }

  private:
    ResourceFactory& underlying_;
    std::latch entered_{1};
    std::latch release_{1};
};

void expect_snapshot(const SnapshotHandle& snapshot, const int workers, const int distance)
{
    REQUIRE(snapshot != nullptr);
    CHECK_EQ(snapshot->settings().worker_threads, workers);
    CHECK_EQ(snapshot->settings().render_distance, distance);
    REQUIRE(snapshot->geometry_buffer() != nullptr);
    REQUIRE(snapshot->lighting_buffer() != nullptr);
    CHECK_EQ(snapshot->geometry_buffer()->size_bytes(), static_cast<std::size_t>(distance) * 256U);
    CHECK_EQ(snapshot->lighting_buffer()->size_bytes(), static_cast<std::size_t>(distance) * 128U);
}

void initial_snapshot_is_complete()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    expect_snapshot(runtime.acquire(), 4, 8);
    CHECK_EQ(ledger->live.load(), 2);
}

void reader_handles_are_immutable_and_owning()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    const SnapshotHandle first = runtime.acquire();
    const SnapshotHandle second = runtime.acquire();
    CHECK(first == second);
    expect_snapshot(first, 4, 8);
}

void successful_publication_replaces_current_snapshot()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    const SnapshotHandle old = runtime.acquire();
    runtime.publish(EngineConfig{16, 24}, factory);
    const SnapshotHandle current = runtime.acquire();
    CHECK(old != current);
    expect_snapshot(current, 16, 24);
    expect_snapshot(old, 4, 8);
    CHECK_EQ(ledger->live.load(), 4);
}

void retiring_old_snapshot_waits_for_its_reader()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    SnapshotHandle held = runtime.acquire();
    runtime.publish(EngineConfig{16, 24}, factory);
    CHECK_EQ(ledger->live.load(), 4);
    held.reset();
    CHECK_EQ(ledger->live.load(), 2);
    CHECK_EQ(ledger->destroyed.load(), 2);
}

void two_readers_extend_old_snapshot_lifetime()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    SnapshotHandle first = runtime.acquire();
    SnapshotHandle second = runtime.acquire();
    runtime.publish(EngineConfig{16, 24}, factory);
    first.reset();
    CHECK_EQ(ledger->live.load(), 4);
    expect_snapshot(second, 4, 8);
    second.reset();
    CHECK_EQ(ledger->live.load(), 2);
}

void first_allocation_failure_preserves_published_snapshot()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    const SnapshotHandle before = runtime.acquire();
    factory.throw_on_call = 3;
    CHECK_THROWS_AS(runtime.publish(EngineConfig{16, 24}, factory), std::runtime_error);
    CHECK(runtime.acquire() == before);
    expect_snapshot(runtime.acquire(), 4, 8);
    CHECK_EQ(ledger->live.load(), 2);
}

void second_allocation_failure_releases_staging()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    const SnapshotHandle before = runtime.acquire();
    factory.throw_on_call = 4;
    CHECK_THROWS_AS(runtime.publish(EngineConfig{16, 24}, factory), std::runtime_error);
    CHECK(runtime.acquire() == before);
    expect_snapshot(runtime.acquire(), 4, 8);
    CHECK_EQ(ledger->created.load(), 3);
    CHECK_EQ(ledger->destroyed.load(), 1);
    CHECK_EQ(ledger->live.load(), 2);
}

void null_geometry_failure_preserves_current()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    const SnapshotHandle before = runtime.acquire();
    factory.null_on_call = 3;
    CHECK_THROWS_AS(runtime.publish(EngineConfig{16, 24}, factory), std::runtime_error);
    CHECK(runtime.acquire() == before);
    CHECK_EQ(ledger->live.load(), 2);
}

void null_lighting_failure_releases_staging()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    const SnapshotHandle before = runtime.acquire();
    factory.null_on_call = 4;
    CHECK_THROWS_AS(runtime.publish(EngineConfig{16, 24}, factory), std::runtime_error);
    CHECK(runtime.acquire() == before);
    CHECK_EQ(ledger->created.load(), 3);
    CHECK_EQ(ledger->destroyed.load(), 1);
    CHECK_EQ(ledger->live.load(), 2);
}

void concurrent_reader_never_sees_empty_during_preparation()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    const SnapshotHandle before = runtime.acquire();
    BlockingFactory blocked{factory};
    std::jthread publisher{[&]() { runtime.publish(EngineConfig{16, 24}, blocked); }};
    blocked.wait_until_blocked();

    // Deterministic observation during the writer's blocking allocation.
    const SnapshotHandle during = runtime.acquire();
    const bool still_old = during == before;
    blocked.unblock();
    publisher.join();

    CHECK(still_old);
    expect_snapshot(runtime.acquire(), 16, 24);
    expect_snapshot(before, 4, 8);
}

void failed_initial_construction_cleans_staging()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    factory.throw_on_call = 2;
    CHECK_THROWS_AS((SnapshotRuntime{EngineConfig{4, 8}, factory}), std::runtime_error);
    CHECK_EQ(ledger->created.load(), 1);
    CHECK_EQ(ledger->destroyed.load(), 1);
    CHECK_EQ(ledger->live.load(), 0);
}

void retained_snapshot_outlives_publisher()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotHandle retained;
    {
        SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
        retained = runtime.acquire();
    }
    expect_snapshot(retained, 4, 8);
    CHECK_EQ(ledger->live.load(), 2);
    retained.reset();
    CHECK_EQ(ledger->live.load(), 0);
}

void publication_can_retry_after_failure()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    factory.throw_on_call = 4;
    CHECK_THROWS_AS(runtime.publish(EngineConfig{16, 24}, factory), std::runtime_error);
    factory.throw_on_call = -1;
    runtime.publish(EngineConfig{16, 24}, factory);
    expect_snapshot(runtime.acquire(), 16, 24);
    CHECK_EQ(ledger->live.load(), 2);
}

void concurrent_readers_see_complete_consistent_snapshots()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    std::atomic<bool> start{false};
    std::atomic<int> invalid{0};
    std::vector<std::jthread> readers;
    for (int index = 0; index < 4; ++index)
    {
        readers.emplace_back(
            [&]()
            {
                start.wait(false, std::memory_order_acquire);
                for (int attempt = 0; attempt < 2000; ++attempt)
                {
                    const auto snap = runtime.acquire();
                    if (!snap || !snap->geometry_buffer() || !snap->lighting_buffer())
                    {
                        invalid.fetch_add(1);
                        continue;
                    }
                    const int distance = snap->settings().render_distance;
                    if (snap->settings().worker_threads < 1 ||
                        snap->settings().worker_threads > 64 || distance < 1 || distance > 32 ||
                        snap->geometry_buffer()->size_bytes() !=
                            static_cast<std::size_t>(distance) * 256U ||
                        snap->lighting_buffer()->size_bytes() !=
                            static_cast<std::size_t>(distance) * 128U)
                    {
                        invalid.fetch_add(1);
                    }
                }
            });
    }
    start.store(true, std::memory_order_release);
    start.notify_all();
    for (int index = 1; index <= 96; ++index)
    {
        runtime.publish(EngineConfig{1 + index % 64, 1 + index % 32}, factory);
    }
    readers.clear(); // jthread destruction joins any remaining readers.
    CHECK_EQ(invalid.load(), 0);
    expect_snapshot(runtime.acquire(), 1 + 96 % 64, 1 + 96 % 32);
    CHECK_EQ(ledger->live.load(), 2);
}

void multiple_publications_retire_each_unused_snapshot()
{
    auto ledger = std::make_shared<Ledger>();
    TrackedFactory factory{ledger};
    SnapshotRuntime runtime{EngineConfig{4, 8}, factory};
    SnapshotHandle first = runtime.acquire();
    runtime.publish(EngineConfig{8, 12}, factory);
    SnapshotHandle second = runtime.acquire();
    runtime.publish(EngineConfig{16, 24}, factory);
    CHECK_EQ(ledger->live.load(), 6);
    second.reset();
    CHECK_EQ(ledger->live.load(), 4);
    first.reset();
    CHECK_EQ(ledger->live.load(), 2);
    CHECK_EQ(ledger->destroyed.load(), 4);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_snapshot_publication_suite()
{
    cpp_mastery::test::TestSuite suite{"snapshot-publication"};
    suite.add("initial snapshot complete", initial_snapshot_is_complete);
    suite.add("reader handles immutable and owning", reader_handles_are_immutable_and_owning);
    suite.add("successful publication", successful_publication_replaces_current_snapshot);
    suite.add("retire after reader releases", retiring_old_snapshot_waits_for_its_reader);
    suite.add("two readers extend lifetime", two_readers_extend_old_snapshot_lifetime);
    suite.add("first failure preserves publication",
              first_allocation_failure_preserves_published_snapshot);
    suite.add("second failure cleans staging", second_allocation_failure_releases_staging);
    suite.add("null geometry preserves publication", null_geometry_failure_preserves_current);
    suite.add("null lighting cleans staging", null_lighting_failure_releases_staging);
    suite.add("blocked preparation keeps old snapshot visible",
              concurrent_reader_never_sees_empty_during_preparation);
    suite.add("failed initial construction cleans staging",
              failed_initial_construction_cleans_staging);
    suite.add("snapshot outlives publisher", retained_snapshot_outlives_publisher);
    suite.add("retry after failed publication", publication_can_retry_after_failure);
    suite.add("concurrent readers see consistent snapshots",
              concurrent_readers_see_complete_consistent_snapshots);
    suite.add("multiple retirements wait for readers",
              multiple_publications_retire_each_unused_snapshot);
    return suite;
}
