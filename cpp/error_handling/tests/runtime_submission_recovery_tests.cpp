#include <cpp_mastery/runtime/SubmissionRecovery.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BackendSubmitOutcome;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::GpuQueueKind;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::ResourceFactory;
using cpp_mastery::runtime::RuntimeSnapshot;
using cpp_mastery::runtime::SubmissionBackend;
using cpp_mastery::runtime::SubmissionDisposition;
using cpp_mastery::runtime::SubmissionRecoveryRuntime;

static_assert(!std::is_copy_constructible_v<SubmissionRecoveryRuntime::Admission>);

struct Counts
{
    int created = 0;
    int destroyed = 0;
    int allocation_calls = 0;
};

class TrackedBuffer final : public RenderBuffer
{
  public:
    explicit TrackedBuffer(Counts& counts) : counts_{counts}
    {
        ++counts_.created;
    }
    ~TrackedBuffer() override
    {
        ++counts_.destroyed;
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return 64;
    }

  private:
    Counts& counts_;
};

class Factory final : public ResourceFactory
{
  public:
    explicit Factory(Counts& counts) : counts_{counts} {}
    int throw_on_call = -1;
    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(BufferKind, int) override
    {
        ++counts_.allocation_calls;
        if (counts_.allocation_calls == throw_on_call)
        {
            throw std::runtime_error{"simulated allocation failure"};
        }
        return std::make_unique<TrackedBuffer>(counts_);
    }

  private:
    Counts& counts_;
};

class Backend final : public SubmissionBackend
{
  public:
    BackendSubmitOutcome outcome = BackendSubmitOutcome::accepted;
    bool throws = false;
    int calls = 0;
    int observed_distance = 0;
    GpuQueueKind observed_queue = GpuQueueKind::graphics;

    BackendSubmitOutcome try_submit(GpuQueueKind queue, const RuntimeSnapshot& snapshot) override
    {
        ++calls;
        observed_queue = queue;
        observed_distance = snapshot.settings().render_distance;
        if (throws)
        {
            throw std::runtime_error{"backend outcome not established"};
        }
        return outcome;
    }
};

struct Fixture
{
    Counts counts;
    Factory factory{counts};
    SubmissionRecoveryRuntime runtime{EngineConfig{4, 8}, factory};
    Backend backend;
};

void accepted_submission_registers_gpu_dependency()
{
    Fixture f;
    const auto result = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::accepted);
    CHECK_EQ(result.gpu_serial, 1U);
    CHECK_EQ(result.incident_id, 0U);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 1U);
    CHECK_EQ(f.runtime.pending_admissions(), 0U);
    CHECK_EQ(f.runtime.unresolved_count(), 0U);
    CHECK_EQ(f.backend.observed_distance, 8);
}

void accepted_retirement_waits_for_completion()
{
    Fixture f;
    const auto result = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::compute, f.backend);
    REQUIRE(result.disposition == SubmissionDisposition::accepted);
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    CHECK_EQ(f.runtime.collect(), 0U);
    CHECK_EQ(f.counts.destroyed, 0);
    f.runtime.complete_through(GpuQueueKind::compute, 1);
    CHECK_EQ(f.runtime.collect(), 1U);
    CHECK_EQ(f.counts.destroyed, 2);
}

void rejection_does_not_invent_gpu_submission()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::not_enqueued;
    const auto result = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::rejected);
    CHECK_EQ(result.gpu_serial, 0U);
    CHECK_EQ(result.incident_id, 0U);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 0U);
    CHECK_EQ(f.runtime.pending_admissions(), 0U);
}

void rejected_old_version_retires_without_gpu_completion()
{
    Fixture f;
    auto ticket = f.runtime.admit();
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    f.backend.outcome = BackendSubmitOutcome::not_enqueued;
    const auto result = f.runtime.try_submit(std::move(ticket), GpuQueueKind::transfer, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::rejected);
    CHECK_EQ(f.runtime.collect(), 1U);
    CHECK_EQ(f.counts.destroyed, 2);
}

void rejected_attempt_preserves_older_gpu_work()
{
    Fixture f;
    CHECK_EQ(f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend).gpu_serial,
             1U);
    auto ticket = f.runtime.admit();
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    f.backend.outcome = BackendSubmitOutcome::not_enqueued;
    const auto result = f.runtime.try_submit(std::move(ticket), GpuQueueKind::graphics, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::rejected);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 1U);
    CHECK_EQ(f.runtime.collect(), 0U);
    f.runtime.complete_through(GpuQueueKind::graphics, 1);
    CHECK_EQ(f.runtime.collect(), 1U);
}

void unknown_retains_admission()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto result = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    REQUIRE(result.disposition == SubmissionDisposition::unresolved);
    CHECK(result.incident_id != 0U);
    CHECK_EQ(result.gpu_serial, 0U);
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    CHECK_EQ(f.runtime.pending_admissions(), 1U);
}

void unknown_does_not_invent_gpu_serial()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto result = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::unresolved);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 0U);
}

void unknown_blocks_retirement_across_publication()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::transfer, f.backend);
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    CHECK_EQ(f.runtime.collect(), 0U);
    CHECK_EQ(f.counts.destroyed, 0);
    if (incident.incident_id != 0)
    {
        f.runtime.confirm_not_enqueued(incident.incident_id);
    }
}

void confirmed_not_enqueued_cancels_without_gpu_use()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::transfer, f.backend);
    REQUIRE(incident.incident_id != 0U);
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    f.runtime.confirm_not_enqueued(incident.incident_id);
    CHECK_EQ(f.runtime.pending_admissions(), 0U);
    CHECK_EQ(f.runtime.unresolved_count(), 0U);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::transfer), 0U);
    CHECK_EQ(f.runtime.collect(), 1U);
}

void confirmed_acceptance_registers_gpu_dependency()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto incident = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::compute, f.backend);
    REQUIRE(incident.incident_id != 0U);
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    CHECK_EQ(f.runtime.confirm_accepted(incident.incident_id), 1U);
    CHECK_EQ(f.runtime.pending_admissions(), 0U);
    CHECK_EQ(f.runtime.unresolved_count(), 0U);
    CHECK_EQ(f.runtime.collect(), 0U);
    f.runtime.complete_through(GpuQueueKind::compute, 1);
    CHECK_EQ(f.runtime.collect(), 1U);
}

void two_uncertain_incidents_require_both_resolutions()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto first = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    const auto second = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::transfer, f.backend);
    REQUIRE(first.incident_id != 0U);
    REQUIRE(second.incident_id != 0U);
    CHECK(first.incident_id != second.incident_id);
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    f.runtime.confirm_not_enqueued(first.incident_id);
    CHECK_EQ(f.runtime.collect(), 0U);
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    f.runtime.confirm_not_enqueued(second.incident_id);
    CHECK_EQ(f.runtime.collect(), 1U);
}

void uncertain_acceptance_across_multiple_publications()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto incident = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::compute, f.backend);
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    f.runtime.publish(EngineConfig{12, 24}, f.factory);
    CHECK_EQ(f.runtime.collect(), 1U);
    REQUIRE(incident.incident_id != 0U);
    CHECK_EQ(f.runtime.confirm_accepted(incident.incident_id), 1U);
    CHECK_EQ(f.runtime.collect(), 0U);
    f.runtime.complete_through(GpuQueueKind::compute, 1);
    CHECK_EQ(f.runtime.collect(), 1U);
}

void backend_exception_is_ambiguous()
{
    Fixture f;
    f.backend.throws = true;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    REQUIRE(incident.disposition == SubmissionDisposition::unresolved);
    REQUIRE(incident.incident_id != 0U);
    CHECK_EQ(f.runtime.pending_admissions(), 1U);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 0U);
    f.runtime.confirm_not_enqueued(incident.incident_id);
    CHECK_EQ(f.runtime.pending_admissions(), 0U);
}

void invalid_incident_ids_are_rejected()
{
    Fixture f;
    CHECK_THROWS_AS(f.runtime.confirm_accepted(100), std::invalid_argument);
    CHECK_THROWS_AS(f.runtime.confirm_not_enqueued(100), std::invalid_argument);
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::transfer, f.backend);
    REQUIRE(incident.incident_id != 0U);
    f.runtime.confirm_not_enqueued(incident.incident_id);
    CHECK_THROWS_AS(f.runtime.confirm_not_enqueued(incident.incident_id), std::invalid_argument);
}

void consumed_incident_cannot_be_resolved_twice()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    REQUIRE(incident.incident_id != 0U);
    CHECK_EQ(f.runtime.confirm_accepted(incident.incident_id), 1U);
    CHECK_THROWS_AS(f.runtime.confirm_accepted(incident.incident_id), std::invalid_argument);
}

void invalid_queue_rejected_before_backend_call()
{
    Fixture f;
    const auto invalid = static_cast<GpuQueueKind>(100);
    CHECK_THROWS_AS(f.runtime.try_submit(f.runtime.admit(), invalid, f.backend),
                    std::invalid_argument);
    CHECK_EQ(f.backend.calls, 0);
    CHECK_EQ(f.runtime.pending_admissions(), 0U);
}

void lost_device_quarantines_unknown_work()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::device_lost;
    const auto result = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::device_lost);
    CHECK(f.runtime.device_lost());
    CHECK(result.incident_id != 0U);
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    CHECK_EQ(f.runtime.pending_admissions(), 1U);
}

void lost_device_blocks_new_admission()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::device_lost;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    CHECK(f.runtime.device_lost());
    CHECK_THROWS_AS(f.runtime.admit(), std::logic_error);
    if (incident.incident_id != 0U)
    {
        f.runtime.confirm_not_enqueued(incident.incident_id);
    }
}

void lost_device_blocks_publication()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::device_lost;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    CHECK_THROWS_AS(f.runtime.publish(EngineConfig{8, 16}, f.factory), std::logic_error);
    CHECK_EQ(f.runtime.acquire()->settings().render_distance, 8);
    if (incident.incident_id != 0U)
    {
        f.runtime.confirm_not_enqueued(incident.incident_id);
    }
}

void lost_device_blocks_new_submission()
{
    Fixture f;
    auto second = f.runtime.admit();
    f.backend.outcome = BackendSubmitOutcome::device_lost;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    CHECK_THROWS_AS(f.runtime.try_submit(std::move(second), GpuQueueKind::compute, f.backend),
                    std::logic_error);
    CHECK_EQ(f.backend.calls, 1);
    if (incident.incident_id != 0U)
    {
        f.runtime.confirm_not_enqueued(incident.incident_id);
    }
}

void lost_device_does_not_forge_completions()
{
    Fixture f;
    const auto accepted =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    REQUIRE(accepted.disposition == SubmissionDisposition::accepted);
    f.backend.outcome = BackendSubmitOutcome::device_lost;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::transfer, f.backend);
    CHECK_THROWS_AS(f.runtime.complete_through(GpuQueueKind::graphics, 1), std::logic_error);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 1U);
    if (incident.incident_id != 0U)
    {
        f.runtime.confirm_not_enqueued(incident.incident_id);
    }
}

void lost_device_freezes_collection_even_after_resolution()
{
    Fixture f;
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    f.backend.outcome = BackendSubmitOutcome::device_lost;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::transfer, f.backend);
    REQUIRE(incident.incident_id != 0U);
    CHECK_EQ(f.runtime.collect(), 0U);
    f.runtime.confirm_not_enqueued(incident.incident_id);
    CHECK_EQ(f.runtime.pending_admissions(), 0U);
    // The old version has no GPU work but the device-loss state is terminal.
    CHECK_EQ(f.runtime.collect(), 0U);
    CHECK_EQ(f.counts.destroyed, 0);
    CHECK_THROWS_AS(f.runtime.complete_through(GpuQueueKind::transfer, 0), std::logic_error);
}

void cpu_reader_extends_lifetime_after_resolution()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    auto old = f.runtime.acquire();
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::graphics, f.backend);
    REQUIRE(incident.incident_id != 0U);
    f.runtime.publish(EngineConfig{8, 16}, f.factory);
    f.runtime.confirm_not_enqueued(incident.incident_id);
    CHECK_EQ(f.runtime.collect(), 1U);
    CHECK_EQ(f.counts.destroyed, 0);
    old.reset();
    CHECK_EQ(f.counts.destroyed, 2);
}

void failed_publication_preserves_uncertain_ticket()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto incident =
        f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::transfer, f.backend);
    REQUIRE(incident.incident_id != 0U);
    f.factory.throw_on_call = f.counts.allocation_calls + 2;
    CHECK_THROWS_AS(f.runtime.publish(EngineConfig{8, 16}, f.factory), std::runtime_error);
    CHECK_EQ(f.runtime.acquire()->settings().render_distance, 8);
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    CHECK_EQ(f.runtime.pending_admissions(), 1U);
    f.runtime.confirm_not_enqueued(incident.incident_id);
}

void rejection_keeps_per_queue_serials_independent()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::not_enqueued;
    const auto rejected = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::compute, f.backend);
    CHECK_EQ(rejected.disposition, SubmissionDisposition::rejected);
    f.backend.outcome = BackendSubmitOutcome::accepted;
    const auto first = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::transfer, f.backend);
    const auto second = f.runtime.try_submit(f.runtime.admit(), GpuQueueKind::compute, f.backend);
    CHECK_EQ(first.gpu_serial, 1U);
    CHECK_EQ(second.gpu_serial, 1U);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::compute), 1U);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::transfer), 1U);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_submission_recovery_suite()
{
    cpp_mastery::test::TestSuite suite{"submission-recovery"};
    suite.add("accepted submission records dependency",
              accepted_submission_registers_gpu_dependency);
    suite.add("accepted work waits for completion", accepted_retirement_waits_for_completion);
    suite.add("confirmed rejection creates no GPU serial",
              rejection_does_not_invent_gpu_submission);
    suite.add("rejected old version can retire",
              rejected_old_version_retires_without_gpu_completion);
    suite.add("rejection preserves earlier work", rejected_attempt_preserves_older_gpu_work);
    suite.add("unknown outcome retains ticket", unknown_retains_admission);
    suite.add("unknown outcome has no invented serial", unknown_does_not_invent_gpu_serial);
    suite.add("unknown outcome blocks retirement", unknown_blocks_retirement_across_publication);
    suite.add("confirmed no enqueue cancels", confirmed_not_enqueued_cancels_without_gpu_use);
    suite.add("confirmed acceptance registers work", confirmed_acceptance_registers_gpu_dependency);
    suite.add("two unknown incidents stay independent",
              two_uncertain_incidents_require_both_resolutions);
    suite.add("unknown survives two publications",
              uncertain_acceptance_across_multiple_publications);
    suite.add("backend exception is ambiguous", backend_exception_is_ambiguous);
    suite.add("invalid recovery incident rejected", invalid_incident_ids_are_rejected);
    suite.add("incident cannot resolve twice", consumed_incident_cannot_be_resolved_twice);
    suite.add("bad queue never reaches backend", invalid_queue_rejected_before_backend_call);
    suite.add("device loss quarantines work", lost_device_quarantines_unknown_work);
    suite.add("device loss blocks admission", lost_device_blocks_new_admission);
    suite.add("device loss blocks publication", lost_device_blocks_publication);
    suite.add("device loss blocks submission", lost_device_blocks_new_submission);
    suite.add("device loss blocks completion", lost_device_does_not_forge_completions);
    suite.add("device loss freezes collection",
              lost_device_freezes_collection_even_after_resolution);
    suite.add("CPU reader extends lifetime", cpu_reader_extends_lifetime_after_resolution);
    suite.add("failed publication keeps incident", failed_publication_preserves_uncertain_ticket);
    suite.add("independent queue serials", rejection_keeps_per_queue_serials_independent);
    return suite;
}
