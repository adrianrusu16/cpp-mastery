#include <cpp_mastery/runtime/DeviceRecoveryRuntime.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace
{

using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::BackendGeneration;
using cpp_mastery::runtime::BackendSubmitOutcome;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::DeviceRecoveryRuntime;
using cpp_mastery::runtime::GpuQueueKind;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::ResourceFactory;
using cpp_mastery::runtime::RuntimeSnapshot;
using cpp_mastery::runtime::SafeTeardownAuthority;
using cpp_mastery::runtime::SubmissionBackend;
using cpp_mastery::runtime::SubmissionDisposition;

struct Counts
{
    int created = 0;
    int destroyed = 0;
    int calls = 0;
};

class Buffer final : public RenderBuffer
{
  public:
    explicit Buffer(Counts& counts) : counts_{counts}
    {
        ++counts_.created;
    }
    ~Buffer() override
    {
        ++counts_.destroyed;
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return 32;
    }

  private:
    Counts& counts_;
};

class Factory final : public ResourceFactory
{
  public:
    explicit Factory(Counts& counts) : counts_{counts} {}
    int fail_call = -1;
    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(BufferKind, int) override
    {
        ++counts_.calls;
        if (counts_.calls == fail_call)
        {
            throw std::runtime_error{"injected new backend allocation failure"};
        }
        return std::make_unique<Buffer>(counts_);
    }

  private:
    Counts& counts_;
};

class Backend final : public SubmissionBackend
{
  public:
    BackendSubmitOutcome outcome = BackendSubmitOutcome::accepted;
    int calls = 0;
    [[nodiscard]] BackendSubmitOutcome try_submit(GpuQueueKind, const RuntimeSnapshot&) override
    {
        ++calls;
        return outcome;
    }
};

class Authority final : public SafeTeardownAuthority
{
  public:
    std::uint64_t approved = 0;
    int calls = 0;
    std::uint64_t requested = 0;
    bool throws = false;
    [[nodiscard]] bool confirmed_safe_to_destroy(const BackendGeneration old) override
    {
        ++calls;
        requested = old.value;
        if (throws)
        {
            throw std::runtime_error{"proof authority unavailable"};
        }
        return approved == old.value;
    }
};

struct Fixture
{
    Counts counts;
    Factory factory{counts};
    DeviceRecoveryRuntime runtime{EngineConfig{4, 8}, factory};
    Backend backend;
    Authority authority;

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
        return runtime.recover(current(), EngineConfig{8, 16}, factory, authority);
    }
};

void initial_generation_has_complete_snapshot()
{
    Fixture f;
    CHECK_EQ(f.current().value, 1U);
    CHECK(!f.runtime.device_lost());
    CHECK_EQ(f.runtime.acquire()->settings().render_distance, 8);
    CHECK_EQ(f.counts.created, 2);
}

void accepted_submission_is_scoped_to_generation()
{
    Fixture f;
    const auto result = f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::accepted);
    CHECK_EQ(result.gpu_serial, 1U);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 1U);
}

void unknown_acceptance_remains_pending_before_loss()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto result = f.runtime.try_submit(f.current(), GpuQueueKind::compute, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::unresolved);
    CHECK(result.incident_id != 0U);
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    CHECK_EQ(f.runtime.pending_admissions(), 1U);
}

void backend_device_loss_quarantines_generation()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::device_lost;
    const auto result = f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.backend);
    CHECK_EQ(result.disposition, SubmissionDisposition::device_lost);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    CHECK_THROWS_AS(f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.backend),
                    std::logic_error);
}

void externally_reported_loss_freezes_operations()
{
    Fixture f;
    f.lose();
    CHECK(f.runtime.device_lost());
    CHECK_THROWS_AS(f.runtime.publish(f.current(), EngineConfig{8, 16}, f.factory),
                    std::logic_error);
    CHECK_THROWS_AS(f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.backend),
                    std::logic_error);
    CHECK_THROWS_AS(f.runtime.complete_through(f.current(), GpuQueueKind::graphics, 0),
                    std::logic_error);
    CHECK_EQ(f.runtime.collect(), 0U);
}

void stale_generation_cannot_report_loss()
{
    Fixture f;
    CHECK_THROWS_AS(f.runtime.report_device_lost(BackendGeneration{2}), std::invalid_argument);
    CHECK(!f.runtime.device_lost());
}

void recovery_requires_device_loss()
{
    Fixture f;
    f.authority.approved = 1;
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.authority.calls, 0);
}

void missing_proof_rejects_recovery()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
}

void unproved_recovery_preserves_generation()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
}

void unproved_recovery_preserves_old_gpu_owners()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.counts.destroyed, 0);
}

void unproved_recovery_does_not_allocate_new_resources()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.counts.calls, 2);
}

void old_unknown_incident_survives_denied_teardown()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto result = f.runtime.try_submit(f.current(), GpuQueueKind::transfer, f.backend);
    REQUIRE(result.incident_id != 0U);
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.runtime.unresolved_count(), 1U);
    CHECK_EQ(f.runtime.pending_admissions(), 1U);
}

void accepted_in_flight_work_survives_denied_teardown()
{
    Fixture f;
    CHECK_EQ(f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.backend).gpu_serial, 1U);
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 1U);
    CHECK_EQ(f.counts.destroyed, 0);
}

void proof_authority_is_consulted()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.authority.calls, 1);
    CHECK_EQ(f.authority.requested, 1U);
}

void throwing_authority_keeps_old_generation()
{
    Fixture f;
    f.lose();
    f.authority.throws = true;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.current().value, 1U);
    CHECK_EQ(f.counts.destroyed, 0);
}

void wrong_generation_proof_is_rejected()
{
    Fixture f;
    f.lose();
    f.authority.approved = 2; // Not proof for old generation 1.
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK_EQ(f.current().value, 1U);
}

void denied_recovery_keeps_cpu_snapshot_current()
{
    Fixture f;
    auto before = f.runtime.acquire();
    f.lose();
    CHECK_THROWS_AS(f.recover(), std::logic_error);
    CHECK(f.runtime.acquire() == before);
}

void explicit_proof_allows_new_generation()
{
    Fixture f;
    f.lose();
    f.authority.approved = 1;
    CHECK_EQ(f.recover().value, 2U);
    CHECK(!f.runtime.device_lost());
    CHECK_EQ(f.runtime.acquire()->settings().render_distance, 16);
}

void verified_teardown_releases_old_gpu_ownership()
{
    Fixture f;
    f.lose();
    f.authority.approved = 1;
    const auto next = f.recover();
    CHECK_EQ(next.value, 2U);
    CHECK_EQ(f.counts.destroyed, 2);
    CHECK_EQ(f.counts.created, 4);
}

void cpu_reader_extends_lifetime_after_safe_teardown()
{
    Fixture f;
    auto old = f.runtime.acquire();
    f.lose();
    f.authority.approved = 1;
    const auto next = f.recover();
    CHECK_EQ(next.value, 2U);
    CHECK_EQ(f.counts.destroyed, 0);
    old.reset();
    CHECK_EQ(f.counts.destroyed, 2);
}

void new_generation_has_independent_serials()
{
    Fixture f;
    CHECK_EQ(f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.backend).gpu_serial, 1U);
    f.lose();
    f.authority.approved = 1;
    const auto next = f.recover();
    CHECK_EQ(f.runtime.submitted(GpuQueueKind::graphics), 0U);
    CHECK_EQ(f.runtime.try_submit(next, GpuQueueKind::graphics, f.backend).gpu_serial, 1U);
}

void stale_submission_cannot_enter_new_backend()
{
    Fixture f;
    const auto old = f.current();
    f.lose();
    f.authority.approved = 1;
    const auto next = f.recover();
    CHECK_THROWS_AS(f.runtime.try_submit(old, GpuQueueKind::graphics, f.backend),
                    std::invalid_argument);
    CHECK_EQ(f.runtime.try_submit(next, GpuQueueKind::graphics, f.backend).gpu_serial, 1U);
    CHECK_EQ(f.backend.calls, 1);
}

void stale_completion_cannot_advance_new_backend()
{
    Fixture f;
    const auto old = f.current();
    f.lose();
    f.authority.approved = 1;
    const auto next = f.recover();
    CHECK_EQ(f.runtime.try_submit(next, GpuQueueKind::transfer, f.backend).gpu_serial, 1U);
    CHECK_THROWS_AS(f.runtime.complete_through(old, GpuQueueKind::transfer, 1),
                    std::invalid_argument);
    f.runtime.complete_through(next, GpuQueueKind::transfer, 1);
}

void stale_publication_cannot_replace_new_snapshot()
{
    Fixture f;
    const auto old = f.current();
    f.lose();
    f.authority.approved = 1;
    const auto next = f.recover();
    CHECK_THROWS_AS(f.runtime.publish(old, EngineConfig{8, 12}, f.factory), std::invalid_argument);
    f.runtime.publish(next, EngineConfig{8, 20}, f.factory);
    CHECK_EQ(f.runtime.acquire()->settings().render_distance, 20);
}

void stale_recovery_request_rejected()
{
    Fixture f;
    f.lose();
    f.authority.approved = 1;
    const auto next = f.recover();
    CHECK_THROWS_AS(
        f.runtime.recover(BackendGeneration{1}, EngineConfig{8, 16}, f.factory, f.authority),
        std::invalid_argument);
    CHECK_EQ(next.value, 2U);
}

void new_preparation_failure_keeps_quarantine()
{
    Fixture f;
    f.lose();
    f.authority.approved = 1;
    f.factory.fail_call = 4; // Fail at second allocation; first staged buffer is cleaned.
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    CHECK_EQ(f.current().value, 1U);
    CHECK(f.runtime.device_lost());
    CHECK_EQ(f.counts.created, 3);
    CHECK_EQ(f.counts.destroyed, 1);
    CHECK_EQ(f.runtime.acquire()->settings().render_distance, 8);
}

void retry_after_preparation_failure_works()
{
    Fixture f;
    f.lose();
    f.authority.approved = 1;
    f.factory.fail_call = 3;
    CHECK_THROWS_AS(f.recover(), std::runtime_error);
    f.factory.fail_call = -1;
    CHECK_EQ(f.recover().value, 2U);
    CHECK_EQ(f.counts.destroyed, 2);
}

void new_generation_can_lose_device_again()
{
    Fixture f;
    f.lose();
    f.authority.approved = 1;
    const auto second = f.recover();
    f.runtime.report_device_lost(second);
    CHECK_THROWS_AS(f.recover(), std::logic_error); // Proof for generation 1 is stale.
    CHECK_EQ(f.current().value, 2U);
    CHECK(f.runtime.device_lost());
    if (f.current().value != 2U || !f.runtime.device_lost())
    {
        // RED already demonstrated premature recovery. Do not trigger a
        // second operation on an unexpectedly running generation.
        return;
    }
    f.authority.approved = 2;
    CHECK_EQ(f.recover().value, 3U);
}

void unresolved_incidents_dropped_only_after_safe_teardown()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto result = f.runtime.try_submit(f.current(), GpuQueueKind::compute, f.backend);
    REQUIRE(result.incident_id != 0U);
    f.lose();
    CHECK_EQ(f.runtime.collect(), 0U);
    f.authority.approved = 1;
    CHECK_EQ(f.recover().value, 2U);
    CHECK_EQ(f.runtime.unresolved_count(), 0U);
    CHECK_EQ(f.runtime.pending_admissions(), 0U);
}

void stale_incident_resolution_is_rejected()
{
    Fixture f;
    f.backend.outcome = BackendSubmitOutcome::unknown;
    const auto result = f.runtime.try_submit(f.current(), GpuQueueKind::graphics, f.backend);
    REQUIRE(result.incident_id != 0U);
    const auto old = f.current();
    f.lose();
    f.authority.approved = 1;
    const auto next = f.recover();
    CHECK_THROWS_AS(f.runtime.confirm_not_enqueued(old, result.incident_id), std::invalid_argument);
    CHECK_THROWS_AS(f.runtime.confirm_accepted(old, result.incident_id), std::invalid_argument);
    CHECK_EQ(next.value, 2U);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_device_recovery_suite()
{
    cpp_mastery::test::TestSuite suite{"device-recovery"};
    suite.add("initial generation has complete snapshot", initial_generation_has_complete_snapshot);
    suite.add("accepted submissions scoped to generation",
              accepted_submission_is_scoped_to_generation);
    suite.add("unknown acceptance remains pending", unknown_acceptance_remains_pending_before_loss);
    suite.add("backend device loss quarantines", backend_device_loss_quarantines_generation);
    suite.add("external loss freezes operations", externally_reported_loss_freezes_operations);
    suite.add("stale loss report rejected", stale_generation_cannot_report_loss);
    suite.add("recovery requires device loss", recovery_requires_device_loss);
    suite.add("missing proof rejects recovery", missing_proof_rejects_recovery);
    suite.add("denial retains generation", unproved_recovery_preserves_generation);
    suite.add("denial retains GPU ownership", unproved_recovery_preserves_old_gpu_owners);
    suite.add("denial avoids new allocations", unproved_recovery_does_not_allocate_new_resources);
    suite.add("unknown incident survives denial", old_unknown_incident_survives_denied_teardown);
    suite.add("accepted in-flight survives denial",
              accepted_in_flight_work_survives_denied_teardown);
    suite.add("proof authority consulted", proof_authority_is_consulted);
    suite.add("throwing authority preserves quarantine", throwing_authority_keeps_old_generation);
    suite.add("wrong generation proof rejected", wrong_generation_proof_is_rejected);
    suite.add("denial preserves CPU snapshot", denied_recovery_keeps_cpu_snapshot_current);
    suite.add("explicit proof starts new generation", explicit_proof_allows_new_generation);
    suite.add("verified teardown releases old ownership",
              verified_teardown_releases_old_gpu_ownership);
    suite.add("CPU reader outlives generation", cpu_reader_extends_lifetime_after_safe_teardown);
    suite.add("fresh generation has independent serials", new_generation_has_independent_serials);
    suite.add("stale submission rejected", stale_submission_cannot_enter_new_backend);
    suite.add("stale completion rejected", stale_completion_cannot_advance_new_backend);
    suite.add("stale publication rejected", stale_publication_cannot_replace_new_snapshot);
    suite.add("stale recovery request rejected", stale_recovery_request_rejected);
    suite.add("failed preparation preserves quarantine", new_preparation_failure_keeps_quarantine);
    suite.add("preparation failure can retry", retry_after_preparation_failure_works);
    suite.add("second loss needs new proof", new_generation_can_lose_device_again);
    suite.add("old incidents released after proof",
              unresolved_incidents_dropped_only_after_safe_teardown);
    suite.add("stale incident resolution rejected", stale_incident_resolution_is_rejected);
    return suite;
}
