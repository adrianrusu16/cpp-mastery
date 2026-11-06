#include <cpp_mastery/runtime/TransactionalBackendRuntime.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BackendGeneration;
using cpp_mastery::runtime::BackendSubmitOutcome;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::GpuQueueKind;
using cpp_mastery::runtime::ReconstructionBackend;
using cpp_mastery::runtime::ReconstructionBuilder;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::RuntimeSnapshot;
using cpp_mastery::runtime::SafeTeardownAuthority;
using cpp_mastery::runtime::SubmissionBackend;
using cpp_mastery::runtime::SubmissionDisposition;
using cpp_mastery::runtime::TransactionalBackendRuntime;

struct Counts
{
    int backends_created = 0;
    int backends_destroyed = 0;
    int buffers_created = 0;
    int buffers_destroyed = 0;
    int builder_calls = 0;
    int allocation_calls = 0;
    std::vector<int> events; // Buffer destructor: +generation; backend destructor: -generation.
};

class Buffer final : public RenderBuffer
{
  public:
    Buffer(std::shared_ptr<Counts> counts, std::uint64_t generation, BufferKind kind)
        : counts_{std::move(counts)}, generation_{generation}, kind_{kind}
    {
        ++counts_->buffers_created;
    }
    ~Buffer() override
    {
        ++counts_->buffers_destroyed;
        counts_->events.push_back(static_cast<int>(generation_));
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return static_cast<std::size_t>(generation_ * 100 +
                                        (kind_ == BufferKind::geometry ? 1U : 2U));
    }

  private:
    std::shared_ptr<Counts> counts_;
    std::uint64_t generation_;
    BufferKind kind_;
};

class Backend final : public ReconstructionBackend
{
  public:
    Backend(std::shared_ptr<Counts> counts, BackendGeneration generation, int fail_call,
            int null_call)
        : counts_{std::move(counts)}, generation_{generation}, fail_call_{fail_call},
          null_call_{null_call}
    {
        ++counts_->backends_created;
    }
    ~Backend() override
    {
        ++counts_->backends_destroyed;
        counts_->events.push_back(-static_cast<int>(generation_.value));
    }
    [[nodiscard]] BackendGeneration generation() const noexcept override
    {
        return generation_;
    }
    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(BufferKind kind, int) override
    {
        ++calls_;
        ++counts_->allocation_calls;
        if (calls_ == fail_call_)
        {
            throw std::runtime_error{"injected backend buffer failure"};
        }
        if (calls_ == null_call_)
        {
            return nullptr;
        }
        return std::make_unique<Buffer>(counts_, generation_.value, kind);
    }

  private:
    std::shared_ptr<Counts> counts_;
    BackendGeneration generation_;
    int fail_call_;
    int null_call_;
    int calls_ = 0;
};

class Builder final : public ReconstructionBuilder
{
  public:
    explicit Builder(std::shared_ptr<Counts> counts) : counts_{std::move(counts)} {}

    bool throw_next = false;
    bool null_next = false;
    bool wrong_generation_next = false;
    int fail_buffer_call = -1;
    int null_buffer_call = -1;

    [[nodiscard]] std::shared_ptr<ReconstructionBackend>
    build(BackendGeneration generation) override
    {
        ++counts_->builder_calls;
        if (throw_next)
        {
            throw_next = false;
            throw std::runtime_error{"injected backend construction failure"};
        }
        if (null_next)
        {
            null_next = false;
            return nullptr;
        }
        if (wrong_generation_next)
        {
            wrong_generation_next = false;
            generation.value += 1;
        }
        const int fail = std::exchange(fail_buffer_call, -1);
        const int null = std::exchange(null_buffer_call, -1);
        return std::make_shared<Backend>(counts_, generation, fail, null);
    }

  private:
    std::shared_ptr<Counts> counts_;
};

class Authority final : public SafeTeardownAuthority
{
  public:
    std::uint64_t approved = 0;
    int calls = 0;
    bool throw_next = false;
    std::uint64_t requested = 0;
    [[nodiscard]] bool confirmed_safe_to_destroy(BackendGeneration generation) override
    {
        ++calls;
        requested = generation.value;
        if (throw_next)
        {
            throw_next = false;
            throw std::runtime_error{"teardown proof unavailable"};
        }
        return approved == generation.value;
    }
};

class SubmitBackend final : public SubmissionBackend
{
  public:
    BackendSubmitOutcome outcome = BackendSubmitOutcome::accepted;
    [[nodiscard]] BackendSubmitOutcome try_submit(GpuQueueKind, const RuntimeSnapshot&) override
    {
        return outcome;
    }
};

struct Fixture
{
    std::shared_ptr<Counts> counts = std::make_shared<Counts>();
    Builder builder{counts};
    TransactionalBackendRuntime runtime{EngineConfig{4, 8}, builder};
    Authority authority;
    SubmitBackend submit;

    [[nodiscard]] BackendGeneration current() const noexcept
    {
        return runtime.generation();
    }
    void lose()
    {
        runtime.report_device_lost(current());
    }
    [[nodiscard]] BackendGeneration recover()
    {
        return runtime.recover(current(), EngineConfig{8, 16}, builder, authority);
    }
    void approve()
    {
        authority.approved = current().value;
    }
};

void initial_backend_and_buffers_are_complete()
{
    Fixture f;
    CHECK_EQ(f.current().value, 1U);
    CHECK_EQ(f.counts->builder_calls, 1);
    CHECK_EQ(f.counts->backends_created, 1);
    CHECK_EQ(f.counts->buffers_created, 2);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 101U);
    CHECK_EQ(f.runtime.acquire()->lighting_buffer()->size_bytes(), 102U);
}

void publication_uses_current_backend()
{
    Fixture f;
    f.runtime.publish(f.current(), EngineConfig{6, 12});
    CHECK_EQ(f.counts->builder_calls, 1);
    CHECK_EQ(f.runtime.acquire()->settings().render_distance, 12);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 101U);
}

void cannot_rebuild_healthy_generation()
{
    Fixture f;
    f.approve();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.counts->builder_calls, 1);
    CHECK_EQ(f.authority.calls, 0);
}

void teardown_denial_precedes_new_backend_build()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.authority.calls, 1);
    CHECK_EQ(f.counts->builder_calls, 1);
    CHECK_EQ(f.counts->buffers_destroyed, 0);
    CHECK(f.runtime.device_lost());
}

void throwing_authority_preserves_old_generation()
{
    Fixture f;
    f.lose();
    f.authority.throw_next = true;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->builder_calls, 1);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
}

void builder_throw_preserves_quarantine()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.throw_next = true;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.counts->buffers_destroyed, 0);
}

void null_backend_preserves_quarantine()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.null_next = true;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.counts->buffers_destroyed, 0);
}

void wrong_backend_generation_rejected()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.wrong_generation_next = true;
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.counts->buffers_destroyed, 0);
}

void failing_geometry_preserves_generation()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.fail_buffer_call = 1;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 101U);
}

void failing_lighting_preserves_generation()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.fail_buffer_call = 2;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.runtime.acquire()->lighting_buffer()->size_bytes(), 102U);
}

void failed_lighting_staging_releases_geometry()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.fail_buffer_call = 2;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->buffers_created, 3);
    CHECK_EQ(f.counts->buffers_destroyed, 1);
    CHECK_EQ(f.counts->backends_created, 2);
    CHECK_EQ(f.counts->backends_destroyed, 1);
}

void failed_geometry_releases_staged_backend()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.fail_buffer_call = 1;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->backends_created, 2);
    CHECK_EQ(f.counts->backends_destroyed, 1);
    CHECK_EQ(f.counts->buffers_destroyed, 0);
}

void null_geometry_preserves_generation()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.null_buffer_call = 1;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
}

void null_lighting_releases_staging()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.null_buffer_call = 2;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK_EQ(f.counts->buffers_created, 3);
    CHECK_EQ(f.counts->buffers_destroyed, 1);
}

void failed_staging_preserves_unresolved_incident()
{
    Fixture f;
    f.submit.outcome = BackendSubmitOutcome::unknown;
    const auto incident = f.runtime.try_submit(f.current(), GpuQueueKind::transfer, f.submit);
    REQUIRE(incident.incident_id != 0U);
    f.lose();
    f.approve();
    f.builder.fail_buffer_call = 2;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    CHECK_EQ(f.current().value, 1U);
}

void failed_staging_preserves_accepted_work()
{
    Fixture f;
    const auto accepted = f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.submit);
    REQUIRE(accepted.disposition == SubmissionDisposition::accepted);
    f.lose();
    f.approve();
    f.builder.fail_buffer_call = 1;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 1U);
    CHECK_EQ(f.current().value, 1U);
}

void retry_after_lighting_failure_succeeds()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.fail_buffer_call = 2;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    if (!f.runtime.device_lost())
    {
        CHECK(f.runtime.device_lost()); // RED: recovery committed before staging finished.
        return;
    }
    CHECK_EQ(f.recover().value, 2U);
    CHECK(!f.runtime.device_lost());
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 201U);
    CHECK_EQ(f.runtime.acquire()->lighting_buffer()->size_bytes(), 202U);
}

void successful_recovery_uses_new_backend()
{
    Fixture f;
    f.lose();
    f.approve();
    CHECK_EQ(f.recover().value, 2U);
    CHECK_EQ(f.counts->builder_calls, 2);
    CHECK_EQ(f.counts->buffers_created, 4);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 201U);
    CHECK_EQ(f.runtime.acquire()->lighting_buffer()->size_bytes(), 202U);
}

void teardown_authority_queried_once_per_attempt()
{
    Fixture f;
    f.lose();
    f.approve();
    CHECK_EQ(f.recover().value, 2U);
    CHECK_EQ(f.authority.calls, 1);
    CHECK_EQ(f.authority.requested, 1U);
}

void old_cpu_reader_retains_backend_lease()
{
    Fixture f;
    auto reader = f.runtime.acquire();
    f.lose();
    f.approve();
    CHECK_EQ(f.recover().value, 2U);
    CHECK_EQ(f.counts->backends_destroyed, 0);
    CHECK_EQ(f.counts->buffers_destroyed, 0);
    CHECK_EQ(reader->geometry_buffer()->size_bytes(), 101U);
    reader.reset();
    CHECK_EQ(f.counts->buffers_destroyed, 2);
    CHECK_EQ(f.counts->backends_destroyed, 1);
}

void buffer_destroyed_before_own_backend()
{
    Fixture f;
    auto reader = f.runtime.acquire();
    f.lose();
    f.approve();
    CHECK_EQ(f.recover().value, 2U);
    reader.reset();
    REQUIRE(f.counts->events.size() >= 3U);
    CHECK_EQ(f.counts->events[0], 1);
    CHECK_EQ(f.counts->events[1], 1);
    CHECK_EQ(f.counts->events[2], -1);
}

void reader_survives_runtime_owner()
{
    const auto counts = std::make_shared<Counts>();
    std::shared_ptr<const RuntimeSnapshot> reader;
    {
        Builder builder{counts};
        TransactionalBackendRuntime runtime{EngineConfig{4, 8}, builder};
        reader = runtime.acquire();
    }
    CHECK_EQ(counts->backends_destroyed, 0);
    CHECK_EQ(reader->geometry_buffer()->size_bytes(), 101U);
    reader.reset();
    CHECK_EQ(counts->buffers_destroyed, 2);
    CHECK_EQ(counts->backends_destroyed, 1);
}

void new_generation_serials_restart()
{
    Fixture f;
    const auto before = f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.submit);
    REQUIRE(before.disposition == SubmissionDisposition::accepted);
    f.lose();
    f.approve();
    const auto next = f.recover();
    const auto after = f.runtime.try_submit(next, GpuQueueKind::graphics, f.submit);
    CHECK_EQ(before.gpu_serial, 1U);
    CHECK_EQ(after.gpu_serial, 1U);
}

void stale_publication_and_submission_rejected()
{
    Fixture f;
    const auto old = f.current();
    f.lose();
    f.approve();
    CHECK_EQ(f.recover().value, 2U);
    CHECK_THROWS_AS(f.runtime.publish(old, EngineConfig{8, 16}), std::invalid_argument);
    CHECK_THROWS_AS(f.runtime.try_submit(old, GpuQueueKind::graphics, f.submit),
                    std::invalid_argument);
}

void stale_recovery_rejected_before_build()
{
    Fixture f;
    const auto old = f.current();
    f.lose();
    f.approve();
    CHECK_EQ(f.recover().value, 2U);
    const int calls = f.counts->builder_calls;
    CHECK_THROWS_AS(f.runtime.recover(old, EngineConfig{8, 16}, f.builder, f.authority),
                    std::invalid_argument);
    CHECK_EQ(f.counts->builder_calls, calls);
}

void second_loss_needs_fresh_proof_and_backend()
{
    Fixture f;
    f.lose();
    f.approve();
    const auto second = f.recover();
    f.runtime.report_device_lost(second);
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.current().value, 2U);
    f.approve();
    CHECK_EQ(f.recover().value, 3U);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 301U);
}

void constructor_geometry_failure_releases_backend()
{
    auto counts = std::make_shared<Counts>();
    Builder builder{counts};
    builder.fail_buffer_call = 1;
    CHECK_THROWS_AS(TransactionalBackendRuntime(EngineConfig{4, 8}, builder), std::runtime_error);
    CHECK_EQ(counts->backends_created, 1);
    CHECK_EQ(counts->backends_destroyed, 1);
    CHECK_EQ(counts->buffers_created, 0);
}

void constructor_lighting_failure_releases_partial_stage()
{
    auto counts = std::make_shared<Counts>();
    Builder builder{counts};
    builder.fail_buffer_call = 2;
    CHECK_THROWS_AS(TransactionalBackendRuntime(EngineConfig{4, 8}, builder), std::runtime_error);
    CHECK_EQ(counts->backends_created, 1);
    CHECK_EQ(counts->backends_destroyed, 1);
    CHECK_EQ(counts->buffers_created, 1);
    CHECK_EQ(counts->buffers_destroyed, 1);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_backend_reconstruction_suite()
{
    cpp_mastery::test::TestSuite suite{"backend-reconstruction"};
    suite.add("initial backend and buffers complete", initial_backend_and_buffers_are_complete);
    suite.add("publication reuses current backend", publication_uses_current_backend);
    suite.add("healthy generation cannot rebuild", cannot_rebuild_healthy_generation);
    suite.add("denial precedes backend build", teardown_denial_precedes_new_backend_build);
    suite.add("throwing authority preserves quarantine",
              throwing_authority_preserves_old_generation);
    suite.add("builder exception preserves quarantine", builder_throw_preserves_quarantine);
    suite.add("null backend preserves quarantine", null_backend_preserves_quarantine);
    suite.add("wrong backend generation rejected", wrong_backend_generation_rejected);
    suite.add("geometry failure preserves generation", failing_geometry_preserves_generation);
    suite.add("lighting failure preserves generation", failing_lighting_preserves_generation);
    suite.add("failed lighting cleans staged geometry", failed_lighting_staging_releases_geometry);
    suite.add("failed geometry cleans staged backend", failed_geometry_releases_staged_backend);
    suite.add("null geometry preserves generation", null_geometry_preserves_generation);
    suite.add("null lighting cleans staging", null_lighting_releases_staging);
    suite.add("unresolved incident survives failure", failed_staging_preserves_unresolved_incident);
    suite.add("accepted GPU work survives failure", failed_staging_preserves_accepted_work);
    suite.add("retry after lighting failure", retry_after_lighting_failure_succeeds);
    suite.add("successful recovery uses new backend", successful_recovery_uses_new_backend);
    suite.add("authority queried once", teardown_authority_queried_once_per_attempt);
    suite.add("CPU reader retains backend lease", old_cpu_reader_retains_backend_lease);
    suite.add("buffers destroyed before backend", buffer_destroyed_before_own_backend);
    suite.add("reader outlives runtime owner", reader_survives_runtime_owner);
    suite.add("new serials restart", new_generation_serials_restart);
    suite.add("stale operations rejected", stale_publication_and_submission_rejected);
    suite.add("stale recovery rejected early", stale_recovery_rejected_before_build);
    suite.add("second loss needs new proof", second_loss_needs_fresh_proof_and_backend);
    suite.add("initial geometry failure cleans backend",
              constructor_geometry_failure_releases_backend);
    suite.add("initial lighting failure cleans backend",
              constructor_lighting_failure_releases_partial_stage);
    return suite;
}
