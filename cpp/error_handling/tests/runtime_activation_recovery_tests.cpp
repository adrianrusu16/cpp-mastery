#include <cpp_mastery/runtime/ActivationRecoveryRuntime.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace
{
using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::ActivatableBackend;
using cpp_mastery::runtime::ActivationBuilder;
using cpp_mastery::runtime::ActivationOutcome;
using cpp_mastery::runtime::ActivationRecoveryRuntime;
using cpp_mastery::runtime::ActivationResolution;
using cpp_mastery::runtime::BackendGeneration;
using cpp_mastery::runtime::BackendSubmitOutcome;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::GpuQueueKind;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::RuntimeSnapshot;
using cpp_mastery::runtime::SafeTeardownAuthority;
using cpp_mastery::runtime::SubmissionBackend;
using cpp_mastery::runtime::SubmissionDisposition;

struct Counts
{
    int builds = 0;
    int built = 0;
    int destroyed = 0;
    int buffers_built = 0;
    int buffers_destroyed = 0;
    int activations = 0;
    int allocations = 0;
};

class Buffer final : public RenderBuffer
{
  public:
    Buffer(std::shared_ptr<Counts> counts, BackendGeneration generation, BufferKind kind)
        : counts_{std::move(counts)}, generation_{generation}, kind_{kind}
    {
        ++counts_->buffers_built;
    }
    ~Buffer() override
    {
        ++counts_->buffers_destroyed;
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return static_cast<std::size_t>(generation_.value * 100 +
                                        (kind_ == BufferKind::geometry ? 1U : 2U));
    }

  private:
    std::shared_ptr<Counts> counts_;
    BackendGeneration generation_;
    BufferKind kind_;
};

struct Settings
{
    ActivationOutcome outcome = ActivationOutcome::started;
    bool throw_activation = false;
    bool throw_builder = false;
    bool null_builder = false;
    bool wrong_generation = false;
    int throw_alloc = -1;
    int null_alloc = -1;
};

class Backend final : public ActivatableBackend
{
  public:
    Backend(std::shared_ptr<Counts> counts, BackendGeneration generation, Settings settings)
        : counts_{std::move(counts)}, generation_{generation}, settings_{settings}
    {
        ++counts_->built;
    }
    ~Backend() override
    {
        ++counts_->destroyed;
    }
    [[nodiscard]] BackendGeneration generation() const noexcept override
    {
        return generation_;
    }
    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(BufferKind kind, int) override
    {
        ++counts_->allocations;
        ++allocations_;
        if (allocations_ == settings_.throw_alloc)
        {
            throw std::runtime_error{"injected buffer failure"};
        }
        if (allocations_ == settings_.null_alloc)
        {
            return nullptr;
        }
        return std::make_unique<Buffer>(counts_, generation_, kind);
    }
    [[nodiscard]] ActivationOutcome activate() override
    {
        ++counts_->activations;
        if (settings_.throw_activation)
        {
            throw std::runtime_error{"ambiguous backend exception"};
        }
        return settings_.outcome;
    }

  private:
    std::shared_ptr<Counts> counts_;
    BackendGeneration generation_;
    Settings settings_;
    int allocations_ = 0;
};

class Builder final : public ActivationBuilder
{
  public:
    explicit Builder(std::shared_ptr<Counts> counts) : counts_{std::move(counts)} {}
    Settings next;

    [[nodiscard]] std::shared_ptr<ActivatableBackend>
    build_activatable(BackendGeneration generation) override
    {
        ++counts_->builds;
        const Settings settings = std::exchange(next, Settings{});
        if (settings.throw_builder)
        {
            throw std::runtime_error{"injected construction failure"};
        }
        if (settings.null_builder)
        {
            return nullptr;
        }
        if (settings.wrong_generation)
        {
            ++generation.value;
        }
        return std::make_shared<Backend>(counts_, generation, settings);
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
    [[nodiscard]] bool confirmed_safe_to_destroy(BackendGeneration generation) override
    {
        ++calls;
        if (throw_next)
        {
            throw_next = false;
            throw std::runtime_error{"authority unavailable"};
        }
        return approved == generation.value;
    }
};

class Submit final : public SubmissionBackend
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
    ActivationRecoveryRuntime runtime{EngineConfig{4, 8}, builder};
    Authority authority;
    Submit submit;

    [[nodiscard]] BackendGeneration current() const noexcept
    {
        return runtime.generation();
    }
    void lose()
    {
        runtime.report_device_lost(current());
    }
    void approve()
    {
        authority.approved = current().value;
    }
    [[nodiscard]] std::optional<BackendGeneration> recover()
    {
        return runtime.recover(current(), EngineConfig{8, 16}, builder, authority);
    }
};

void initial_generation_is_valid()
{
    Fixture f;
    CHECK_EQ(f.current().value, 1U);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 101U);
    CHECK_EQ(f.runtime.acquire()->lighting_buffer()->size_bytes(), 102U);
    CHECK_EQ(f.counts->activations, 0);
}
void cannot_recover_healthy()
{
    Fixture f;
    f.approve();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.counts->builds, 1);
}
void stale_generation_rejected()
{
    Fixture f;
    f.lose();
    f.approve();
    CHECK_THROWS_AS(
        f.runtime.recover(BackendGeneration{17}, EngineConfig{8, 16}, f.builder, f.authority),
        std::invalid_argument);
    CHECK_EQ(f.authority.calls, 0);
}
void denied_teardown_prevents_build()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.counts->builds, 1);
    CHECK_EQ(f.counts->activations, 0);
}
void throwing_authority_preserves_quarantine()
{
    Fixture f;
    f.lose();
    f.authority.throw_next = true;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.counts->builds, 1);
}
void failed_builder_does_not_activate()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.throw_builder = true;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->activations, 0);
    CHECK_EQ(f.current().value, 1U);
}
void null_builder_rejected()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.null_builder = true;
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.current().value, 1U);
}
void wrong_generation_builder_rejected()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.wrong_generation = true;
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.current().value, 1U);
}
void failing_geometry_never_activates()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.throw_alloc = 1;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->activations, 0);
    CHECK_EQ(f.counts->destroyed, 1);
}
void failing_lighting_releases_staging()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.throw_alloc = 2;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->activations, 0);
    CHECK_EQ(f.counts->buffers_destroyed, 1);
    CHECK_EQ(f.current().value, 1U);
}
void null_geometry_rejected()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.null_alloc = 1;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->activations, 0);
}
void null_lighting_releases_geometry()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.null_alloc = 2;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->buffers_destroyed, 1);
}
void rejected_activation_retains_old_generation()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::not_started;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
    CHECK(!f.runtime.activation_pending());
}
void rejected_activation_releases_prepared_backend()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::not_started;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->destroyed, 1);
    CHECK_EQ(f.counts->buffers_destroyed, 2);
    CHECK_EQ(f.counts->activations, 1);
}
void rejected_activation_does_not_release_old_resources()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::not_started;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.counts->buffers_built, 4);
    CHECK_EQ(f.counts->buffers_destroyed, 2);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 101U);
}
void rejected_activation_can_retry()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::not_started;
    const auto old = f.current();
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    if (f.current() != old)
    {
        CHECK_EQ(f.current().value, old.value);
        return;
    }
    const auto next = f.recover();
    REQUIRE(next.has_value());
    CHECK_EQ(next->value, 2U);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 201U);
}
void rejected_activation_preserves_old_cpu_snapshot()
{
    Fixture f;
    const auto old_snapshot = f.runtime.acquire();
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::not_started;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK(f.runtime.acquire() == old_snapshot);
}
void rejected_activation_stays_frozen()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::not_started;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_THROWS_AS(f.runtime.publish(f.current(), EngineConfig{5, 10}), std::logic_error);
}
void rejected_activation_does_not_create_new_serials()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::not_started;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 0U);
    CHECK_EQ(f.current().value, 1U);
}
void unknown_retains_staged_backend()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    const auto result = f.recover();
    CHECK(!result.has_value());
    CHECK(f.runtime.activation_pending());
    CHECK_EQ(f.counts->destroyed, 0);
    CHECK_EQ(f.counts->buffers_destroyed, 0);
    CHECK_EQ(f.current().value, 1U);
}
void throwing_activation_is_unknown()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.throw_activation = true;
    const auto result = f.recover();
    CHECK(!result.has_value());
    CHECK(f.runtime.activation_pending());
    CHECK_EQ(f.current().value, 1U);
}
void invalid_backend_result_is_unknown()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = static_cast<ActivationOutcome>(-1);
    const auto result = f.recover();
    CHECK(!result.has_value());
    CHECK(f.runtime.activation_pending());
    CHECK_EQ(f.current().value, 1U);
}
void unknown_blocks_retry_without_resolution()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    (void)f.recover();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.counts->activations, 1);
}
void unknown_does_not_allow_publication()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    (void)f.recover();
    CHECK_THROWS_AS(f.runtime.publish(f.current(), EngineConfig{7, 14}), std::logic_error);
}
void unknown_confirmed_not_started_drops_stage()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    (void)f.recover();
    const auto result = f.runtime.resolve_pending(ActivationResolution::confirmed_not_started);
    CHECK(!result.has_value());
    CHECK(!f.runtime.activation_pending());
    CHECK_EQ(f.counts->destroyed, 1);
    CHECK_EQ(f.counts->buffers_destroyed, 2);
    CHECK_EQ(f.current().value, 1U);
}
void unknown_resolution_allows_retry()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    (void)f.recover();
    (void)f.runtime.resolve_pending(ActivationResolution::confirmed_not_started);
    const auto result = f.recover();
    REQUIRE(result.has_value());
    CHECK_EQ(result->value, 2U);
}
void unknown_confirmed_started_commits()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    (void)f.recover();
    const auto result = f.runtime.resolve_pending(ActivationResolution::confirmed_started);
    REQUIRE(result.has_value());
    CHECK_EQ(result->value, 2U);
    CHECK(!f.runtime.activation_pending());
    CHECK_EQ(f.runtime.acquire()->lighting_buffer()->size_bytes(), 202U);
}
void successful_activation_reuses_prepared_resources()
{
    Fixture f;
    f.lose();
    f.approve();
    const auto result = f.recover();
    REQUIRE(result.has_value());
    CHECK_EQ(f.counts->buffers_built, 4);
    CHECK_EQ(f.counts->allocations, 4);
    CHECK_EQ(f.counts->activations, 1);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 201U);
}
void later_publication_uses_activated_backend()
{
    Fixture f;
    f.lose();
    f.approve();
    const auto result = f.recover();
    REQUIRE(result.has_value());
    f.runtime.publish(*result, EngineConfig{9, 24});
    CHECK_EQ(f.counts->allocations, 6);
    CHECK_EQ(f.runtime.acquire()->settings().render_distance, 24);
}
void old_reader_preserves_old_backend_lifetime()
{
    Fixture f;
    const auto reader = f.runtime.acquire();
    f.lose();
    f.approve();
    REQUIRE(f.recover().has_value());
    CHECK_EQ(f.counts->destroyed, 0);
    CHECK_EQ(reader->geometry_buffer()->size_bytes(), 101U);
}
void old_gpu_uses_still_preserved_on_rejection()
{
    Fixture f;
    const auto old = f.current();
    const auto submitted = f.runtime.try_submit(old, GpuQueueKind::graphics, f.submit);
    REQUIRE(submitted.disposition == SubmissionDisposition::accepted);
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::not_started;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 1U);
    CHECK_EQ(f.current().value, 1U);
}
void unknown_old_incident_stays_quarantined()
{
    Fixture f;
    f.submit.outcome = BackendSubmitOutcome::unknown;
    const auto incident = f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.submit);
    REQUIRE(incident.disposition == SubmissionDisposition::unresolved);
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    (void)f.recover();
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    CHECK_EQ(f.current().value, 1U);
}
void wrong_resolution_is_rejected()
{
    Fixture f;
    CHECK_THROWS_AS(f.runtime.resolve_pending(ActivationResolution::confirmed_started),
                    std::logic_error);
}
void duplicate_resolution_is_rejected()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    (void)f.recover();
    (void)f.runtime.resolve_pending(ActivationResolution::confirmed_not_started);
    CHECK_THROWS_AS(f.runtime.resolve_pending(ActivationResolution::confirmed_not_started),
                    std::logic_error);
}
void invalid_resolution_does_not_discard_stage()
{
    Fixture f;
    f.lose();
    f.approve();
    f.builder.next.outcome = ActivationOutcome::unknown;
    (void)f.recover();
    CHECK_THROWS_AS(f.runtime.resolve_pending(static_cast<ActivationResolution>(-1)),
                    std::invalid_argument);
    CHECK(f.runtime.activation_pending());
}
void next_loss_requires_fresh_proof()
{
    Fixture f;
    f.lose();
    f.approve();
    const auto second = f.recover();
    REQUIRE(second.has_value());
    f.runtime.report_device_lost(*second);
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    f.approve();
    const auto third = f.recover();
    REQUIRE(third.has_value());
    CHECK_EQ(third->value, 3U);
}
void backend_submission_serials_restart_after_activation()
{
    Fixture f;
    const auto first = f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.submit);
    REQUIRE(first.disposition == SubmissionDisposition::accepted);
    f.lose();
    f.approve();
    const auto second = f.recover();
    REQUIRE(second.has_value());
    const auto next = f.runtime.try_submit(*second, GpuQueueKind::graphics, f.submit);
    CHECK_EQ(first.gpu_serial, 1U);
    CHECK_EQ(next.gpu_serial, 1U);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_activation_recovery_suite()
{
    cpp_mastery::test::TestSuite suite{"activation-recovery"};
    suite.add("initial generation valid", initial_generation_is_valid);
    suite.add("healthy generation cannot recover", cannot_recover_healthy);
    suite.add("stale generation rejected", stale_generation_rejected);
    suite.add("teardown denial prevents build", denied_teardown_prevents_build);
    suite.add("throwing authority preserves quarantine", throwing_authority_preserves_quarantine);
    suite.add("builder failure before activation", failed_builder_does_not_activate);
    suite.add("null backend rejected", null_builder_rejected);
    suite.add("wrong backend generation rejected", wrong_generation_builder_rejected);
    suite.add("geometry failure before activation", failing_geometry_never_activates);
    suite.add("lighting failure cleans staging", failing_lighting_releases_staging);
    suite.add("null geometry rejected", null_geometry_rejected);
    suite.add("null lighting cleans geometry", null_lighting_releases_geometry);
    suite.add("confirmed no-start keeps quarantine", rejected_activation_retains_old_generation);
    suite.add("confirmed no-start frees new backend",
              rejected_activation_releases_prepared_backend);
    suite.add("confirmed no-start keeps old buffers",
              rejected_activation_does_not_release_old_resources);
    suite.add("confirmed no-start supports retry", rejected_activation_can_retry);
    suite.add("no-start keeps published snapshot", rejected_activation_preserves_old_cpu_snapshot);
    suite.add("no-start remains frozen", rejected_activation_stays_frozen);
    suite.add("no-start preserves serial identity",
              rejected_activation_does_not_create_new_serials);
    suite.add("unknown retains staged backend", unknown_retains_staged_backend);
    suite.add("throwing activation is unknown", throwing_activation_is_unknown);
    suite.add("invalid provider result is unknown", invalid_backend_result_is_unknown);
    suite.add("unknown blocks retry", unknown_blocks_retry_without_resolution);
    suite.add("unknown blocks old publication", unknown_does_not_allow_publication);
    suite.add("unknown confirmed no-start discards", unknown_confirmed_not_started_drops_stage);
    suite.add("unknown confirmed no-start retries", unknown_resolution_allows_retry);
    suite.add("unknown confirmed started commits", unknown_confirmed_started_commits);
    suite.add("ready activation reuses buffers", successful_activation_reuses_prepared_resources);
    suite.add("later publication uses activated backend", later_publication_uses_activated_backend);
    suite.add("old CPU reader extends backend life", old_reader_preserves_old_backend_lifetime);
    suite.add("old GPU dependency survives rejection", old_gpu_uses_still_preserved_on_rejection);
    suite.add("old unknown incident stays quarantined", unknown_old_incident_stays_quarantined);
    suite.add("resolution requires pending", wrong_resolution_is_rejected);
    suite.add("resolution cannot repeat", duplicate_resolution_is_rejected);
    suite.add("invalid resolution retains stage", invalid_resolution_does_not_discard_stage);
    suite.add("second loss needs fresh proof", next_loss_requires_fresh_proof);
    suite.add("new generation restarts serials",
              backend_submission_serials_restart_after_activation);
    return suite;
}
