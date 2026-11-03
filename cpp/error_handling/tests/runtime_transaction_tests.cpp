#include <cpp_mastery/runtime/RuntimeState.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::CpuResourceFactory;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::ResourceFactory;
using cpp_mastery::runtime::RuntimeSettings;
using cpp_mastery::runtime::RuntimeState;

static_assert(std::is_nothrow_move_assignable_v<RuntimeSettings>);
static_assert(noexcept(std::declval<std::unique_ptr<RenderBuffer>&>().swap(
    std::declval<std::unique_ptr<RenderBuffer>&>())));
static_assert(!noexcept(std::declval<RuntimeState&>().apply(std::declval<const EngineConfig&>(),
                                                            std::declval<ResourceFactory&>())));

struct Counters
{
    int created = 0;
    int destroyed = 0;
    int live = 0;
    int calls = 0;
    int geometry_calls = 0;
    int lighting_calls = 0;
};

class TrackedBuffer final : public RenderBuffer
{
  public:
    TrackedBuffer(Counters& counters, const std::size_t bytes) : counters_{counters}, bytes_{bytes}
    {
        ++counters_.created;
        ++counters_.live;
    }

    ~TrackedBuffer() override
    {
        ++counters_.destroyed;
        --counters_.live;
    }

    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return bytes_;
    }

  private:
    Counters& counters_;
    std::size_t bytes_;
};

class ControlledFactory final : public ResourceFactory
{
  public:
    explicit ControlledFactory(Counters& counters) : counters_{counters} {}

    int throw_on_call = -1;
    int null_on_call = -1;

    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(const BufferKind kind,
                                                         const int render_distance) override
    {
        ++counters_.calls;
        if (kind == BufferKind::geometry)
        {
            ++counters_.geometry_calls;
        }
        else
        {
            ++counters_.lighting_calls;
        }

        if (counters_.calls == throw_on_call)
        {
            throw std::runtime_error{"injected resource allocation failure"};
        }
        if (counters_.calls == null_on_call)
        {
            return nullptr;
        }
        return std::make_unique<TrackedBuffer>(counters_,
                                               static_cast<std::size_t>(render_distance) * 32U);
    }

  private:
    Counters& counters_;
};

void expect_settings(const RuntimeState& state, const int workers, const int distance)
{
    CHECK_EQ(state.settings().worker_threads, workers);
    CHECK_EQ(state.settings().render_distance, distance);
}

void constructor_prepares_both_buffers()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    expect_settings(state, 4, 8);
    REQUIRE(state.geometry_buffer() != nullptr);
    REQUIRE(state.lighting_buffer() != nullptr);
    CHECK(state.geometry_buffer() != state.lighting_buffer());
    CHECK_EQ(counters.live, 2);
    CHECK_EQ(counters.geometry_calls, 1);
    CHECK_EQ(counters.lighting_calls, 1);
}

void successfully_applies_both_settings()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    const auto* old_geometry = state.geometry_buffer();
    const auto* old_lighting = state.lighting_buffer();
    state.apply(EngineConfig{16, 24}, factory);
    expect_settings(state, 16, 24);
    CHECK(state.geometry_buffer() != old_geometry);
    CHECK(state.lighting_buffer() != old_lighting);
    CHECK_EQ(counters.created, 4);
    CHECK_EQ(counters.destroyed, 2);
    CHECK_EQ(counters.live, 2);
}

void failure_on_first_allocation_preserves_state()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    const auto* old_geometry = state.geometry_buffer();
    const auto* old_lighting = state.lighting_buffer();
    factory.throw_on_call = 3;
    CHECK_THROWS_AS(state.apply(EngineConfig{16, 24}, factory), std::runtime_error);
    expect_settings(state, 4, 8);
    CHECK(state.geometry_buffer() == old_geometry);
    CHECK(state.lighting_buffer() == old_lighting);
    CHECK_EQ(counters.live, 2);
    CHECK_EQ(counters.destroyed, 0);
}

void failure_on_second_allocation_cleans_staged_buffer()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    const auto* old_geometry = state.geometry_buffer();
    const auto* old_lighting = state.lighting_buffer();
    factory.throw_on_call = 4;
    CHECK_THROWS_AS(state.apply(EngineConfig{16, 24}, factory), std::runtime_error);
    expect_settings(state, 4, 8);
    CHECK(state.geometry_buffer() == old_geometry);
    CHECK(state.lighting_buffer() == old_lighting);
    CHECK_EQ(counters.created, 3);
    CHECK_EQ(counters.destroyed, 1);
    CHECK_EQ(counters.live, 2);
}

void null_first_allocation_preserves_state()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    const auto* old_geometry = state.geometry_buffer();
    factory.null_on_call = 3;
    CHECK_THROWS_AS(state.apply(EngineConfig{16, 24}, factory), std::runtime_error);
    expect_settings(state, 4, 8);
    CHECK(state.geometry_buffer() == old_geometry);
    CHECK_EQ(counters.live, 2);
}

void null_second_allocation_cleans_first()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    const auto* old_geometry = state.geometry_buffer();
    factory.null_on_call = 4;
    CHECK_THROWS_AS(state.apply(EngineConfig{16, 24}, factory), std::runtime_error);
    expect_settings(state, 4, 8);
    CHECK(state.geometry_buffer() == old_geometry);
    CHECK_EQ(counters.created, 3);
    CHECK_EQ(counters.destroyed, 1);
    CHECK_EQ(counters.live, 2);
}

void exception_does_not_invalidate_existing_resources()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    factory.throw_on_call = 4;
    CHECK_THROWS_AS(state.apply(EngineConfig{16, 24}, factory), std::runtime_error);
    REQUIRE(state.geometry_buffer() != nullptr);
    REQUIRE(state.lighting_buffer() != nullptr);
    CHECK_EQ(state.geometry_buffer()->size_bytes(), 8U * 32U);
    CHECK_EQ(state.lighting_buffer()->size_bytes(), 8U * 32U);
}

void failed_update_preserves_most_recent_committed_state()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    state.apply(EngineConfig{8, 12}, factory);
    const auto* committed_geometry = state.geometry_buffer();
    const auto* committed_lighting = state.lighting_buffer();
    factory.throw_on_call = 6;
    CHECK_THROWS_AS(state.apply(EngineConfig{16, 24}, factory), std::runtime_error);
    expect_settings(state, 8, 12);
    CHECK(state.geometry_buffer() == committed_geometry);
    CHECK(state.lighting_buffer() == committed_lighting);
    CHECK_EQ(counters.live, 2);
}

void retry_after_failure_can_commit_successfully()
{
    Counters counters;
    ControlledFactory factory{counters};
    RuntimeState state{EngineConfig{4, 8}, factory};
    factory.throw_on_call = 4;
    CHECK_THROWS_AS(state.apply(EngineConfig{16, 24}, factory), std::runtime_error);
    factory.throw_on_call = -1;
    state.apply(EngineConfig{16, 24}, factory);
    expect_settings(state, 16, 24);
    CHECK_EQ(counters.live, 2);
}

void initial_construction_failure_releases_staged_buffer()
{
    Counters counters;
    ControlledFactory factory{counters};
    factory.throw_on_call = 2;
    CHECK_THROWS_AS((RuntimeState{EngineConfig{4, 8}, factory}), std::runtime_error);
    CHECK_EQ(counters.created, 1);
    CHECK_EQ(counters.destroyed, 1);
    CHECK_EQ(counters.live, 0);
}

void release_on_state_destruction()
{
    Counters counters;
    ControlledFactory factory{counters};
    {
        RuntimeState state{EngineConfig{4, 8}, factory};
        state.apply(EngineConfig{16, 24}, factory);
        CHECK_EQ(counters.live, 2);
    }
    CHECK_EQ(counters.created, counters.destroyed);
    CHECK_EQ(counters.live, 0);
}

void cpu_factory_prepares_actual_backing_storage()
{
    CpuResourceFactory factory;
    RuntimeState state{EngineConfig{4, 8}, factory};
    REQUIRE(state.geometry_buffer() != nullptr);
    REQUIRE(state.lighting_buffer() != nullptr);
    CHECK_EQ(state.geometry_buffer()->size_bytes(), 8U * 1024U);
    CHECK_EQ(state.lighting_buffer()->size_bytes(), 8U * 512U);
    state.apply(EngineConfig{16, 24}, factory);
    expect_settings(state, 16, 24);
    CHECK_EQ(state.geometry_buffer()->size_bytes(), 24U * 1024U);
    CHECK_EQ(state.lighting_buffer()->size_bytes(), 24U * 512U);
}

void cpu_factory_rejects_invalid_distance()
{
    CpuResourceFactory factory;
    RuntimeState state{EngineConfig{4, 8}, factory};
    const auto* old_geometry = state.geometry_buffer();
    CHECK_THROWS_AS(state.apply(EngineConfig{16, 33}, factory), std::invalid_argument);
    expect_settings(state, 4, 8);
    CHECK(state.geometry_buffer() == old_geometry);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_transaction_suite()
{
    cpp_mastery::test::TestSuite suite{"transaction"};
    suite.add("construction prepares both buffers", constructor_prepares_both_buffers);
    suite.add("successful update replaces both", successfully_applies_both_settings);
    suite.add("first allocation failure preserves state",
              failure_on_first_allocation_preserves_state);
    suite.add("second failure releases staging", failure_on_second_allocation_cleans_staged_buffer);
    suite.add("null first allocation preserves state", null_first_allocation_preserves_state);
    suite.add("null second allocation releases staging", null_second_allocation_cleans_first);
    suite.add("active resources remain usable", exception_does_not_invalidate_existing_resources);
    suite.add("preserve most recent committed state",
              failed_update_preserves_most_recent_committed_state);
    suite.add("retry after failure", retry_after_failure_can_commit_successfully);
    suite.add("failed initial construction cleans up",
              initial_construction_failure_releases_staged_buffer);
    suite.add("state destruction releases resources", release_on_state_destruction);
    suite.add("CPU factory allocates backing storage", cpu_factory_prepares_actual_backing_storage);
    suite.add("CPU factory rejects invalid distance", cpu_factory_rejects_invalid_distance);
    return suite;
}
