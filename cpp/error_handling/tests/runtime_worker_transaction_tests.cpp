#include <cpp_mastery/runtime/WorkerRuntime.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::ParkedWorkerPool;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::ResourceFactory;
using cpp_mastery::runtime::WorkerRuntime;
using cpp_mastery::runtime::WorkerStats;

static_assert(noexcept(std::declval<ParkedWorkerPool&>().activate()));
static_assert(!noexcept(std::declval<WorkerRuntime&>().apply(
    std::declval<const EngineConfig&>(), std::declval<ResourceFactory&>(),
    std::declval<std::shared_ptr<WorkerStats>>())));

struct BufferCounts
{
    int created = 0;
    int destroyed = 0;
    int live = 0;
    int calls = 0;
};

class TrackedBuffer final : public RenderBuffer
{
  public:
    TrackedBuffer(BufferCounts& counts, const std::size_t bytes) : counts_{counts}, bytes_{bytes}
    {
        ++counts_.created;
        ++counts_.live;
    }
    ~TrackedBuffer() override
    {
        ++counts_.destroyed;
        --counts_.live;
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return bytes_;
    }

  private:
    BufferCounts& counts_;
    std::size_t bytes_;
};

class CountingFactory final : public ResourceFactory
{
  public:
    explicit CountingFactory(BufferCounts& counts) : counts_{counts} {}
    int throw_on_call = -1;

    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(const BufferKind,
                                                         const int distance) override
    {
        ++counts_.calls;
        if (counts_.calls == throw_on_call)
        {
            throw std::runtime_error{"injected buffer creation error"};
        }
        return std::make_unique<TrackedBuffer>(counts_, static_cast<std::size_t>(distance) * 32U);
    }

  private:
    BufferCounts& counts_;
};

[[nodiscard]] std::shared_ptr<WorkerStats> fresh_stats()
{
    return std::make_shared<WorkerStats>();
}

[[nodiscard]] bool await_at_least(const std::atomic<int>& counter, const int expected)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (counter.load(std::memory_order_acquire) < expected)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return true;
}

[[nodiscard]] ParkedWorkerPool::BeforeLaunch fail_launch(const int failed_index)
{
    return [failed_index](const int index)
    {
        if (index == failed_index)
        {
            throw std::system_error{
                std::make_error_code(std::errc::resource_unavailable_try_again)};
        }
    };
}

void expect_initial_state(const WorkerRuntime& runtime)
{
    CHECK_EQ(runtime.state().settings().worker_threads, 4);
    CHECK_EQ(runtime.state().settings().render_distance, 8);
    CHECK_EQ(runtime.active_workers(), 4);
    REQUIRE(runtime.state().geometry_buffer() != nullptr);
    REQUIRE(runtime.state().lighting_buffer() != nullptr);
    CHECK_EQ(runtime.state().geometry_buffer()->size_bytes(), 8U * 32U);
    CHECK_EQ(runtime.state().lighting_buffer()->size_bytes(), 8U * 32U);
}

void initial_pool_executes_tasks_after_activation()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    auto stats = fresh_stats();
    WorkerRuntime runtime{EngineConfig{4, 8}, factory, stats};
    expect_initial_state(runtime);
    CHECK(await_at_least(stats->tasks_executed, 4));
    CHECK_EQ(stats->parked.load(), 4);
    CHECK_EQ(stats->tasks_executed.load(), 4);
    CHECK_EQ(counts.live, 2);
}

void successful_update_commits_new_pool_and_resources()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    auto old_stats = fresh_stats();
    WorkerRuntime runtime{EngineConfig{4, 8}, factory, old_stats};
    REQUIRE(await_at_least(old_stats->tasks_executed, 4));
    const auto* previous_geometry = runtime.state().geometry_buffer();
    const auto* previous_lighting = runtime.state().lighting_buffer();

    auto fresh = fresh_stats();
    runtime.apply(EngineConfig{8, 24}, factory, fresh);
    CHECK_EQ(runtime.state().settings().worker_threads, 8);
    CHECK_EQ(runtime.state().settings().render_distance, 24);
    CHECK_EQ(runtime.active_workers(), 8);
    CHECK(runtime.state().geometry_buffer() != previous_geometry);
    CHECK(runtime.state().lighting_buffer() != previous_lighting);
    CHECK(await_at_least(fresh->tasks_executed, 8));
    CHECK_EQ(fresh->parked.load(), 8);
    CHECK_EQ(fresh->tasks_executed.load(), 8);
    CHECK_EQ(old_stats->exited.load(), 4);
    CHECK_EQ(counts.live, 2);
    CHECK_EQ(counts.destroyed, 2);
}

void failed_launch_preserves_old_state(const int failed_index)
{
    BufferCounts counts;
    CountingFactory factory{counts};
    auto original = fresh_stats();
    WorkerRuntime runtime{EngineConfig{4, 8}, factory, original};
    REQUIRE(await_at_least(original->tasks_executed, 4));
    const auto* geometry = runtime.state().geometry_buffer();
    const auto* lighting = runtime.state().lighting_buffer();

    auto staged = fresh_stats();
    CHECK_THROWS_AS(runtime.apply(EngineConfig{8, 24}, factory, staged, fail_launch(failed_index)),
                    std::system_error);
    expect_initial_state(runtime);
    CHECK(runtime.state().geometry_buffer() == geometry);
    CHECK(runtime.state().lighting_buffer() == lighting);
    CHECK_EQ(staged->parked.load(), failed_index - 1);
    CHECK_EQ(staged->exited.load(), failed_index - 1);
    CHECK_EQ(staged->tasks_executed.load(), 0);
    CHECK_EQ(counts.created, 4);
    CHECK_EQ(counts.destroyed, 2);
    CHECK_EQ(counts.live, 2);
}

void first_worker_fails_to_launch()
{
    failed_launch_preserves_old_state(1);
}
void second_worker_fails_to_launch()
{
    failed_launch_preserves_old_state(2);
}
void fifth_worker_fails_after_four_park()
{
    failed_launch_preserves_old_state(5);
}
void eighth_worker_fails_after_seven_park()
{
    failed_launch_preserves_old_state(8);
}

void fifth_launch_observes_four_parked_and_zero_side_effects()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    WorkerRuntime runtime{EngineConfig{4, 8}, factory, fresh_stats()};
    auto staged = fresh_stats();
    bool observed_four_parked = false;
    CHECK_THROWS_AS(runtime.apply(EngineConfig{8, 24}, factory, staged,
                                  [&](const int index)
                                  {
                                      if (index == 5)
                                      {
                                          observed_four_parked = staged->parked.load() == 4 &&
                                                                 staged->tasks_executed.load() == 0;
                                          throw std::system_error{std::make_error_code(
                                              std::errc::resource_unavailable_try_again)};
                                      }
                                  }),
                    std::system_error);
    CHECK(observed_four_parked);
    CHECK_EQ(staged->exited.load(), 4);
    CHECK_EQ(staged->tasks_executed.load(), 0);
    expect_initial_state(runtime);
}

void first_buffer_failure_never_launches_threads()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    WorkerRuntime runtime{EngineConfig{4, 8}, factory, fresh_stats()};
    factory.throw_on_call = 3;
    auto staged = fresh_stats();
    int launches = 0;
    CHECK_THROWS_AS(
        runtime.apply(EngineConfig{8, 24}, factory, staged, [&](const int) { ++launches; }),
        std::runtime_error);
    expect_initial_state(runtime);
    CHECK_EQ(launches, 0);
    CHECK_EQ(staged->parked.load(), 0);
    CHECK_EQ(staged->tasks_executed.load(), 0);
    CHECK_EQ(counts.live, 2);
}

void second_buffer_failure_cleans_first_without_launching_threads()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    WorkerRuntime runtime{EngineConfig{4, 8}, factory, fresh_stats()};
    factory.throw_on_call = 4;
    auto staged = fresh_stats();
    int launches = 0;
    CHECK_THROWS_AS(
        runtime.apply(EngineConfig{8, 24}, factory, staged, [&](const int) { ++launches; }),
        std::runtime_error);
    expect_initial_state(runtime);
    CHECK_EQ(launches, 0);
    CHECK_EQ(staged->parked.load(), 0);
    CHECK_EQ(staged->tasks_executed.load(), 0);
    CHECK_EQ(counts.created, 3);
    CHECK_EQ(counts.destroyed, 1);
    CHECK_EQ(counts.live, 2);
}

void failed_initial_pool_construction_releases_all_resources()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    auto staged = fresh_stats();
    CHECK_THROWS_AS((WorkerRuntime{EngineConfig{8, 24}, factory, staged, fail_launch(5)}),
                    std::system_error);
    CHECK_EQ(staged->parked.load(), 4);
    CHECK_EQ(staged->exited.load(), 4);
    CHECK_EQ(staged->tasks_executed.load(), 0);
    CHECK_EQ(counts.created, 2);
    CHECK_EQ(counts.destroyed, 2);
    CHECK_EQ(counts.live, 0);
}

void retry_after_failure_succeeds()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    WorkerRuntime runtime{EngineConfig{4, 8}, factory, fresh_stats()};
    auto failed = fresh_stats();
    CHECK_THROWS_AS(runtime.apply(EngineConfig{8, 24}, factory, failed, fail_launch(5)),
                    std::system_error);
    auto succeeding = fresh_stats();
    runtime.apply(EngineConfig{8, 24}, factory, succeeding);
    CHECK_EQ(runtime.state().settings().worker_threads, 8);
    CHECK_EQ(runtime.state().settings().render_distance, 24);
    CHECK_EQ(runtime.active_workers(), 8);
    CHECK(await_at_least(succeeding->tasks_executed, 8));
    CHECK_EQ(failed->tasks_executed.load(), 0);
    CHECK_EQ(counts.live, 2);
}

void failure_preserves_latest_committed_state()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    WorkerRuntime runtime{EngineConfig{4, 8}, factory, fresh_stats()};
    auto committed = fresh_stats();
    runtime.apply(EngineConfig{6, 12}, factory, committed);
    REQUIRE(await_at_least(committed->tasks_executed, 6));
    auto failed = fresh_stats();
    CHECK_THROWS_AS(runtime.apply(EngineConfig{8, 24}, factory, failed, fail_launch(5)),
                    std::system_error);
    CHECK_EQ(runtime.active_workers(), 6);
    CHECK_EQ(runtime.state().settings().worker_threads, 6);
    CHECK_EQ(runtime.state().settings().render_distance, 12);
    CHECK_EQ(failed->parked.load(), 4);
    CHECK_EQ(failed->exited.load(), 4);
    CHECK_EQ(failed->tasks_executed.load(), 0);
    CHECK_EQ(counts.live, 2);
}

void state_destruction_joins_workers_before_releasing_buffers()
{
    BufferCounts counts;
    CountingFactory factory{counts};
    auto stats = fresh_stats();
    {
        WorkerRuntime runtime{EngineConfig{4, 8}, factory, stats};
        CHECK(await_at_least(stats->tasks_executed, 4));
        CHECK_EQ(counts.live, 2);
    }
    CHECK_EQ(stats->exited.load(), 4);
    CHECK_EQ(counts.live, 0);
    CHECK_EQ(counts.created, counts.destroyed);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_worker_pool_suite()
{
    cpp_mastery::test::TestSuite suite{"worker-pool"};
    suite.add("initial pool activates after construction",
              initial_pool_executes_tasks_after_activation);
    suite.add("successful update replaces entire bundle",
              successful_update_commits_new_pool_and_resources);
    suite.add("first launch failure leaves active state", first_worker_fails_to_launch);
    suite.add("second launch failure joins staging", second_worker_fails_to_launch);
    suite.add("fifth launch failure joins four workers", fifth_worker_fails_after_four_park);
    suite.add("eighth launch failure joins seven workers", eighth_worker_fails_after_seven_park);
    suite.add("fifth launch sees four parked no tasks",
              fifth_launch_observes_four_parked_and_zero_side_effects);
    suite.add("first allocation failure starts no workers",
              first_buffer_failure_never_launches_threads);
    suite.add("second allocation failure releases staging",
              second_buffer_failure_cleans_first_without_launching_threads);
    suite.add("failed initial construction joins workers",
              failed_initial_pool_construction_releases_all_resources);
    suite.add("retry after failure commits successfully", retry_after_failure_succeeds);
    suite.add("failure preserves most recent commit", failure_preserves_latest_committed_state);
    suite.add("destruction joins and releases buffers",
              state_destruction_joins_workers_before_releasing_buffers);
    return suite;
}
