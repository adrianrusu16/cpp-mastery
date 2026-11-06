#include <cpp_mastery/runtime/IdempotentActivationRuntime.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::ActivatableBackend;
using cpp_mastery::runtime::ActivationAttemptId;
using cpp_mastery::runtime::ActivationAttemptResult;
using cpp_mastery::runtime::ActivationAttemptState;
using cpp_mastery::runtime::ActivationBuilder;
using cpp_mastery::runtime::ActivationOutcome;
using cpp_mastery::runtime::ActivationResolution;
using cpp_mastery::runtime::BackendGeneration;
using cpp_mastery::runtime::BufferKind;
using cpp_mastery::runtime::IdempotentActivationRuntime;
using cpp_mastery::runtime::RenderBuffer;
using cpp_mastery::runtime::SafeTeardownAuthority;

struct Counts
{
    int builds = 0;
    int activations = 0;
    int buffers_created = 0;
    int buffers_destroyed = 0;
    int backend_destroyed = 0;
};

class Buffer final : public RenderBuffer
{
  public:
    Buffer(std::shared_ptr<Counts> counts, BackendGeneration generation, BufferKind kind)
        : counts_{std::move(counts)}, generation_{generation}, kind_{kind}
    {
        ++counts_->buffers_created;
    }
    ~Buffer() override
    {
        ++counts_->buffers_destroyed;
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return static_cast<std::size_t>(generation_.value * 100U +
                                        (kind_ == BufferKind::geometry ? 1U : 2U));
    }

  private:
    std::shared_ptr<Counts> counts_;
    BackendGeneration generation_;
    BufferKind kind_;
};

class Backend final : public ActivatableBackend
{
  public:
    Backend(std::shared_ptr<Counts> counts, BackendGeneration generation, ActivationOutcome outcome,
            bool throwing)
        : counts_{std::move(counts)}, generation_{generation}, outcome_{outcome},
          throwing_{throwing}
    {
    }
    ~Backend() override
    {
        ++counts_->backend_destroyed;
    }
    [[nodiscard]] BackendGeneration generation() const noexcept override
    {
        return generation_;
    }
    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(BufferKind kind, int) override
    {
        return std::make_unique<Buffer>(counts_, generation_, kind);
    }
    [[nodiscard]] ActivationOutcome activate() override
    {
        ++counts_->activations;
        if (throwing_)
        {
            throw std::runtime_error{"backend activation response lost"};
        }
        return outcome_;
    }

  private:
    std::shared_ptr<Counts> counts_;
    BackendGeneration generation_;
    ActivationOutcome outcome_;
    bool throwing_;
};

class Builder final : public ActivationBuilder
{
  public:
    explicit Builder(std::shared_ptr<Counts> counts) : counts_{std::move(counts)} {}
    ActivationOutcome next_outcome = ActivationOutcome::started;
    bool next_throwing = false;
    bool next_fail = false;

    [[nodiscard]] std::shared_ptr<ActivatableBackend>
    build_activatable(BackendGeneration generation) override
    {
        ++counts_->builds;
        if (std::exchange(next_fail, false))
        {
            throw std::runtime_error{"failed backend preparation"};
        }
        return std::make_shared<Backend>(counts_, generation,
                                         std::exchange(next_outcome, ActivationOutcome::started),
                                         std::exchange(next_throwing, false));
    }

  private:
    std::shared_ptr<Counts> counts_;
};

class Authority final : public SafeTeardownAuthority
{
  public:
    std::uint64_t approved = 0;
    int calls = 0;
    [[nodiscard]] bool confirmed_safe_to_destroy(BackendGeneration old) override
    {
        ++calls;
        return old.value == approved;
    }
};

struct Fixture
{
    std::shared_ptr<Counts> counts = std::make_shared<Counts>();
    Builder builder{counts};
    IdempotentActivationRuntime runtime{EngineConfig{4, 8}, builder};
    Authority authority;
    static constexpr EngineConfig config{8, 16};

    [[nodiscard]] BackendGeneration current() const noexcept
    {
        return runtime.generation();
    }
    void lose()
    {
        runtime.report_device_lost(current());
        authority.approved = current().value;
    }
    [[nodiscard]] ActivationAttemptResult run(std::uint64_t id)
    {
        return runtime.recover_once(ActivationAttemptId{id}, current(), config, builder, authority);
    }
    [[nodiscard]] ActivationAttemptResult resolve(std::uint64_t id, ActivationResolution answer)
    {
        return runtime.resolve(ActivationAttemptId{id}, answer);
    }
    void unknown()
    {
        lose();
        builder.next_outcome = ActivationOutcome::unknown;
        const auto attempt = run(41);
        REQUIRE(attempt.state == ActivationAttemptState::pending);
    }
};

void initial_generation()
{
    Fixture f;
    CHECK_EQ(f.current().value, 1U);
    CHECK_EQ(f.counts->builds, 1);
    CHECK_EQ(f.runtime.recorded_attempts(), 0U);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 101U);
}
void require_loss()
{
    Fixture f;
    CHECK_THROWS_AS(f.run(1), std::logic_error);
    CHECK_EQ(f.runtime.recorded_attempts(), 0U);
}
void reject_zero_id()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.run(0), std::invalid_argument);
    CHECK_EQ(f.counts->builds, 1);
}
void reject_stale_generation_without_record()
{
    Fixture f;
    f.lose();
    CHECK_THROWS_AS(f.runtime.recover_once({8}, {9}, Fixture::config, f.builder, f.authority),
                    std::invalid_argument);
    CHECK_EQ(f.runtime.recorded_attempts(), 0U);
}
void started_commits()
{
    Fixture f;
    f.lose();
    const auto r = f.run(1);
    CHECK(r.state == ActivationAttemptState::committed);
    CHECK_EQ(r.generation.value, 2U);
    CHECK_EQ(f.counts->activations, 1);
}
void unknown_has_stable_identity()
{
    Fixture f;
    f.unknown();
    CHECK(f.runtime.activation_pending());
    CHECK_EQ(f.runtime.recorded_attempts(), 1U);
    CHECK_EQ(f.counts->activations, 1);
}
void unknown_replay_is_read_only()
{
    Fixture f;
    f.unknown();
    const auto r = f.runtime.recover_once({41}, {1}, Fixture::config, f.builder, f.authority);
    CHECK(r.state == ActivationAttemptState::pending);
    CHECK_EQ(r.id.value, 41U);
    CHECK_EQ(f.counts->activations, 1);
    CHECK_EQ(f.counts->builds, 2);
}
void unknown_replay_skips_authority()
{
    Fixture f;
    f.unknown();
    const auto calls = f.authority.calls;
    f.authority.approved = 0;
    const auto r = f.runtime.recover_once({41}, {1}, Fixture::config, f.builder, f.authority);
    CHECK(r.state == ActivationAttemptState::pending);
    CHECK_EQ(f.authority.calls, calls);
}
void repeat_unknown_many_times()
{
    Fixture f;
    f.unknown();
    for (int n = 0; n < 8; ++n)
    {
        const auto r = f.runtime.recover_once({41}, {1}, Fixture::config, f.builder, f.authority);
        CHECK(r.state == ActivationAttemptState::pending);
    }
    CHECK_EQ(f.counts->activations, 1);
    CHECK_EQ(f.runtime.recorded_attempts(), 1U);
}
void distinct_key_blocked_while_pending()
{
    Fixture f;
    f.unknown();
    CHECK_THROWS_AS(f.run(42), std::logic_error);
    CHECK_EQ(f.runtime.recorded_attempts(), 1U);
    CHECK_EQ(f.counts->activations, 1);
}
void pending_conflicting_config_rejected()
{
    Fixture f;
    f.unknown();
    CHECK_THROWS_AS(f.runtime.recover_once({41}, {1}, EngineConfig{8, 20}, f.builder, f.authority),
                    std::invalid_argument);
    CHECK_EQ(f.counts->activations, 1);
}
void pending_conflicting_generation_rejected()
{
    Fixture f;
    f.unknown();
    CHECK_THROWS_AS(f.runtime.recover_once({41}, {2}, Fixture::config, f.builder, f.authority),
                    std::invalid_argument);
}
void unknown_confirm_no_start()
{
    Fixture f;
    f.unknown();
    const auto r = f.resolve(41, ActivationResolution::confirmed_not_started);
    CHECK(r.state == ActivationAttemptState::not_started);
    CHECK_EQ(r.generation.value, 0U);
    CHECK(!f.runtime.activation_pending());
    CHECK_EQ(f.current().value, 1U);
}
void no_start_resolution_idempotent()
{
    Fixture f;
    f.unknown();
    const auto first = f.resolve(41, ActivationResolution::confirmed_not_started);
    const auto second = f.resolve(41, ActivationResolution::confirmed_not_started);
    CHECK(first.state == second.state);
    CHECK_EQ(f.counts->activations, 1);
}
void no_start_request_replay_not_reactivated()
{
    Fixture f;
    f.unknown();
    const auto initial = f.resolve(41, ActivationResolution::confirmed_not_started);
    const auto repeat = f.runtime.recover_once({41}, {1}, Fixture::config, f.builder, f.authority);
    CHECK(initial.state == repeat.state);
    CHECK_EQ(f.counts->activations, 1);
    CHECK_EQ(f.counts->builds, 2);
}
void new_key_permits_new_attempt()
{
    Fixture f;
    f.unknown();
    static_cast<void>(f.resolve(41, ActivationResolution::confirmed_not_started));
    const auto next = f.run(42);
    CHECK(next.state == ActivationAttemptState::committed);
    CHECK_EQ(f.counts->activations, 2);
    CHECK_EQ(f.current().value, 2U);
}
void unknown_confirm_started()
{
    Fixture f;
    f.unknown();
    const auto result = f.resolve(41, ActivationResolution::confirmed_started);
    CHECK(result.state == ActivationAttemptState::committed);
    CHECK_EQ(result.generation.value, 2U);
    CHECK_EQ(f.counts->activations, 1);
}
void started_resolution_idempotent()
{
    Fixture f;
    f.unknown();
    const auto first = f.resolve(41, ActivationResolution::confirmed_started);
    const auto second = f.resolve(41, ActivationResolution::confirmed_started);
    CHECK(first.state == second.state);
    CHECK_EQ(first.generation.value, second.generation.value);
}
void started_request_replay_after_commit()
{
    Fixture f;
    f.lose();
    const auto first = f.run(11);
    const auto replay = f.runtime.recover_once({11}, {1}, Fixture::config, f.builder, f.authority);
    CHECK(replay.state == ActivationAttemptState::committed);
    CHECK_EQ(replay.generation.value, first.generation.value);
    CHECK_EQ(f.counts->activations, 1);
}
void started_request_replay_after_later_loss()
{
    Fixture f;
    f.lose();
    static_cast<void>(f.run(11));
    f.lose();
    const auto replay = f.runtime.recover_once({11}, {1}, Fixture::config, f.builder, f.authority);
    CHECK_EQ(replay.generation.value, 2U);
    CHECK_EQ(f.counts->activations, 1);
}
void old_key_replay_after_later_commit()
{
    Fixture f;
    f.lose();
    static_cast<void>(f.run(11));
    f.lose();
    static_cast<void>(f.run(12));
    const auto replay = f.runtime.recover_once({11}, {1}, Fixture::config, f.builder, f.authority);
    CHECK_EQ(replay.generation.value, 2U);
    CHECK_EQ(f.current().value, 3U);
    CHECK_EQ(f.counts->activations, 2);
}
void wrong_resolution_id_preserves_pending()
{
    Fixture f;
    f.unknown();
    CHECK_THROWS_AS(f.resolve(42, ActivationResolution::confirmed_started), std::invalid_argument);
    CHECK(f.runtime.activation_pending());
}
void contradictory_terminal_resolution_rejected()
{
    Fixture f;
    f.unknown();
    static_cast<void>(f.resolve(41, ActivationResolution::confirmed_not_started));
    CHECK_THROWS_AS(f.resolve(41, ActivationResolution::confirmed_started), std::logic_error);
}
void invalid_resolution_preserves_pending()
{
    Fixture f;
    f.unknown();
    CHECK_THROWS_AS(f.resolve(41, static_cast<ActivationResolution>(99)), std::invalid_argument);
    CHECK(f.runtime.activation_pending());
    CHECK(f.resolve(41, ActivationResolution::confirmed_started).state ==
          ActivationAttemptState::committed);
}
void confirmed_rejection_idempotent()
{
    Fixture f;
    f.lose();
    f.builder.next_outcome = ActivationOutcome::not_started;
    CHECK_THROWS_AS(f.run(51), std::runtime_error);
    const auto again = f.runtime.recover_once({51}, {1}, Fixture::config, f.builder, f.authority);
    CHECK(again.state == ActivationAttemptState::not_started);
    CHECK_EQ(f.counts->activations, 1);
}
void backend_exception_is_unknown()
{
    Fixture f;
    f.lose();
    f.builder.next_throwing = true;
    const auto first = f.run(61);
    CHECK(first.state == ActivationAttemptState::pending);
    const auto again = f.runtime.recover_once({61}, {1}, Fixture::config, f.builder, f.authority);
    CHECK(again.state == ActivationAttemptState::pending);
    CHECK_EQ(f.counts->activations, 1);
}
void failed_preparation_does_not_reuse_id()
{
    Fixture f;
    f.lose();
    f.builder.next_fail = true;
    CHECK_THROWS_AS(f.run(71), std::runtime_error);
    const auto replay = f.runtime.recover_once({71}, {1}, Fixture::config, f.builder, f.authority);
    CHECK(replay.state == ActivationAttemptState::not_started);
    CHECK_EQ(f.counts->builds, 2);
    CHECK_EQ(f.counts->activations, 0);
}
void failed_preparation_allows_new_id()
{
    Fixture f;
    f.lose();
    f.builder.next_fail = true;
    CHECK_THROWS_AS(f.run(71), std::runtime_error);
    CHECK(f.run(72).state == ActivationAttemptState::committed);
    CHECK_EQ(f.counts->activations, 1);
}
void proof_denial_no_side_effects()
{
    Fixture f;
    f.lose();
    f.authority.approved = 0;
    CHECK_THROWS_AS(f.run(81), std::logic_error);
    CHECK_EQ(f.counts->builds, 1);
    CHECK_EQ(f.counts->activations, 0);
    CHECK_EQ(f.runtime.recorded_attempts(), 1U);
}
void conflicting_terminal_input_rejected()
{
    Fixture f;
    f.lose();
    static_cast<void>(f.run(12));
    CHECK_THROWS_AS(f.runtime.recover_once({12}, {1}, EngineConfig{9, 16}, f.builder, f.authority),
                    std::invalid_argument);
}
void reader_retains_old_buffers()
{
    Fixture f;
    const auto reader = f.runtime.acquire();
    f.lose();
    static_cast<void>(f.run(12));
    CHECK_EQ(reader->geometry_buffer()->size_bytes(), 101U);
    CHECK_EQ(f.runtime.acquire()->geometry_buffer()->size_bytes(), 201U);
    CHECK_EQ(f.counts->buffers_destroyed, 0);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_idempotent_activation_suite()
{
    cpp_mastery::test::TestSuite suite{"idempotent-activation"};
    suite.add("initial generation", initial_generation);
    suite.add("require device loss", require_loss);
    suite.add("reject zero attempt ID", reject_zero_id);
    suite.add("reject stale generation", reject_stale_generation_without_record);
    suite.add("confirmed activation commits", started_commits);
    suite.add("unknown has stable identity", unknown_has_stable_identity);
    suite.add("unknown replay is read only", unknown_replay_is_read_only);
    suite.add("unknown replay skips authority", unknown_replay_skips_authority);
    suite.add("repeated unknown replay", repeat_unknown_many_times);
    suite.add("new identity blocked while pending", distinct_key_blocked_while_pending);
    suite.add("pending input conflict", pending_conflicting_config_rejected);
    suite.add("pending generation conflict", pending_conflicting_generation_rejected);
    suite.add("unknown confirmed no start", unknown_confirm_no_start);
    suite.add("no start resolution idempotent", no_start_resolution_idempotent);
    suite.add("no start request replay", no_start_request_replay_not_reactivated);
    suite.add("new ID allows genuine retry", new_key_permits_new_attempt);
    suite.add("unknown confirmed started", unknown_confirm_started);
    suite.add("started resolution idempotent", started_resolution_idempotent);
    suite.add("committed request replay", started_request_replay_after_commit);
    suite.add("committed replay across loss", started_request_replay_after_later_loss);
    suite.add("old replay after next commit", old_key_replay_after_later_commit);
    suite.add("wrong resolution ID", wrong_resolution_id_preserves_pending);
    suite.add("contradictory resolution", contradictory_terminal_resolution_rejected);
    suite.add("invalid resolution", invalid_resolution_preserves_pending);
    suite.add("confirmed rejection replay", confirmed_rejection_idempotent);
    suite.add("backend exception is ambiguous", backend_exception_is_unknown);
    suite.add("failed preparation replay", failed_preparation_does_not_reuse_id);
    suite.add("failed preparation fresh ID", failed_preparation_allows_new_id);
    suite.add("proof denial", proof_denial_no_side_effects);
    suite.add("terminal input conflict", conflicting_terminal_input_rejected);
    suite.add("CPU reader retains buffers", reader_retains_old_buffers);
    return suite;
}
