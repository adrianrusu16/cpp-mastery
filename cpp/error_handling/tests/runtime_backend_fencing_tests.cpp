#include <cpp_mastery/runtime/FencedBackendHandoff.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
using namespace cpp_mastery::runtime;

constexpr AuthorityEpoch old_owner{7};
constexpr AuthorityEpoch new_owner{8};
constexpr AuthorityEpoch later_owner{12};

class TempDirectory final
{
  public:
    TempDirectory()
    {
        std::array<char, 64> name{};
        const std::string pattern = "/tmp/cpp-mastery-l20-XXXXXX";
        pattern.copy(name.data(), pattern.size());
        if (::mkdtemp(name.data()) == nullptr)
        {
            throw std::runtime_error{"cannot create temporary test directory"};
        }
        path_ = name.data();
    }
    ~TempDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    [[nodiscard]] std::filesystem::path epoch_file() const
    {
        return path_ / "authority.epochs";
    }

  private:
    std::filesystem::path path_;
};

class EpochHook final : public EpochWriteHook
{
  public:
    bool fail_next = false;
    int calls = 0;
    void before_append(AuthorityEpoch) override
    {
        ++calls;
        if (fail_next)
        {
            throw std::runtime_error{"injected pre-append failure"};
        }
    }
};

class AckHook final : public HandoffAckHook
{
  public:
    bool fail = false;
    int calls = 0;
    AuthorityEpoch last{};
    void after_persist(const AuthorityEpoch epoch) override
    {
        ++calls;
        last = epoch;
        if (fail)
        {
            throw std::runtime_error{"injected crash after durable handoff"};
        }
    }
};

class Sink final : public BackendCommandSink
{
  public:
    int effects = 0;
    std::vector<std::uint64_t> ids;
    std::function<void(FencedBackendCommand)> on_execute;
    void execute(const FencedBackendCommand command) override
    {
        ++effects;
        ids.push_back(command.command_id);
        if (on_execute)
        {
            on_execute(command);
        }
    }
};

struct Fixture
{
    TempDirectory temp;
    EpochHook epoch_hook;
    AckHook ack;
    Sink sink;
    std::unique_ptr<AuthorityEpochLedger> ledger;
    std::unique_ptr<FencedBackendGate> gate;

    explicit Fixture(const bool initial_epoch = true)
    {
        reopen();
        if (initial_epoch)
        {
            ledger->advance(old_owner);
        }
    }
    void reopen()
    {
        gate.reset();
        ledger.reset();
        ledger = std::make_unique<AuthorityEpochLedger>(temp.epoch_file(), &epoch_hook);
        gate = std::make_unique<FencedBackendGate>(*ledger);
    }
    [[nodiscard]] BackendAdmission send(const AuthorityEpoch token, const std::uint64_t id = 1)
    {
        return gate->submit({token, id}, sink);
    }
    [[nodiscard]] HandoffDisposition rotate(const AuthorityEpoch token = new_owner)
    {
        return gate->handoff(token, &ack);
    }
};

void no_owner_rejects_commands()
{
    Fixture f{false};
    CHECK(f.send(old_owner) == BackendAdmission::rejected_stale);
    CHECK_EQ(f.sink.effects, 0);
}
void current_owner_executes()
{
    Fixture f;
    CHECK(f.send(old_owner) == BackendAdmission::executed);
    CHECK_EQ(f.sink.effects, 1);
}
void effect_receives_correlated_command()
{
    Fixture f;
    f.sink.on_execute = [](const FencedBackendCommand command)
    {
        CHECK_EQ(command.owner.value, old_owner.value);
        CHECK_EQ(command.command_id, 63U);
    };
    CHECK(f.send(old_owner, 63) == BackendAdmission::executed);
    CHECK_EQ(f.sink.ids.at(0), 63U);
}
void multiple_authorized_operations()
{
    Fixture f;
    for (std::uint64_t i = 1; i <= 3; ++i)
    {
        CHECK(f.send(old_owner, i) == BackendAdmission::executed);
    }
    CHECK_EQ(f.sink.effects, 3);
}
void command_zero_id_refused()
{
    Fixture f;
    CHECK_THROWS_AS(f.send(old_owner, 0), std::invalid_argument);
    CHECK_EQ(f.sink.effects, 0);
}
void zero_token_refused()
{
    Fixture f;
    CHECK(f.send({0}) == BackendAdmission::rejected_stale);
    CHECK_EQ(f.sink.effects, 0);
}
void future_token_refused()
{
    Fixture f;
    CHECK(f.send(new_owner) == BackendAdmission::rejected_stale);
    CHECK_EQ(f.sink.effects, 0);
}
void handoff_is_durable()
{
    Fixture f;
    CHECK(f.rotate() == HandoffDisposition::advanced);
    CHECK_EQ(f.ledger->current().value, new_owner.value);
    f.reopen();
    CHECK_EQ(f.ledger->current().value, new_owner.value);
}
void acknowledgement_after_durable_write()
{
    Fixture f;
    CHECK(f.rotate() == HandoffDisposition::advanced);
    CHECK_EQ(f.ack.calls, 1);
    CHECK_EQ(f.ack.last.value, new_owner.value);
}
void equal_epoch_replays()
{
    Fixture f;
    static_cast<void>(f.rotate());
    CHECK(f.rotate() == HandoffDisposition::already_durable);
}
void replay_does_not_repeat_acknowledgement()
{
    Fixture f;
    static_cast<void>(f.rotate());
    static_cast<void>(f.rotate());
    CHECK_EQ(f.ack.calls, 1);
    CHECK_EQ(f.epoch_hook.calls, 2);
}
void rollback_refused()
{
    Fixture f;
    static_cast<void>(f.rotate());
    CHECK_THROWS_AS(f.rotate(old_owner), std::invalid_argument);
    CHECK_EQ(f.ledger->current().value, new_owner.value);
}
void zero_handoff_refused()
{
    Fixture f;
    CHECK_THROWS_AS(f.rotate({0}), std::invalid_argument);
    CHECK_EQ(f.ledger->current().value, old_owner.value);
}
void epoch_skips_allowed()
{
    Fixture f;
    CHECK(f.rotate(later_owner) == HandoffDisposition::advanced);
    CHECK_EQ(f.ledger->current().value, later_owner.value);
}
void stale_owner_after_handoff_refused()
{
    Fixture f;
    static_cast<void>(f.rotate());
    CHECK(f.send(old_owner) == BackendAdmission::rejected_stale);
}
void stale_owner_after_multiple_handoffs_refused()
{
    Fixture f;
    static_cast<void>(f.rotate());
    static_cast<void>(f.rotate(later_owner));
    CHECK(f.send(old_owner) == BackendAdmission::rejected_stale);
    CHECK(f.send(new_owner) == BackendAdmission::rejected_stale);
}
void stale_request_has_no_effect()
{
    Fixture f;
    static_cast<void>(f.rotate());
    static_cast<void>(f.send(old_owner, 1));
    CHECK_EQ(f.sink.effects, 0);
}
void replacement_owner_admitted()
{
    Fixture f;
    static_cast<void>(f.rotate());
    CHECK(f.send(new_owner) == BackendAdmission::executed);
}
void higher_token_not_self_authorizing()
{
    Fixture f;
    CHECK(f.send(later_owner) == BackendAdmission::rejected_stale);
    CHECK_EQ(f.ledger->current().value, old_owner.value);
}
void future_token_waits_for_handoff()
{
    Fixture f;
    CHECK(f.send(new_owner) == BackendAdmission::rejected_stale);
    static_cast<void>(f.rotate());
    CHECK(f.send(new_owner) == BackendAdmission::executed);
}
void lost_ack_still_persisted()
{
    Fixture f;
    f.ack.fail = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    CHECK_EQ(f.ledger->current().value, new_owner.value);
    CHECK(!f.gate->quarantined());
}
void lost_ack_blocks_old_owner()
{
    Fixture f;
    f.ack.fail = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    CHECK(f.send(old_owner) == BackendAdmission::rejected_stale);
    CHECK_EQ(f.sink.effects, 0);
}
void restart_refuses_old_token()
{
    Fixture f;
    static_cast<void>(f.rotate());
    f.reopen();
    CHECK(f.send(old_owner) == BackendAdmission::rejected_stale);
}
void restart_admits_new_token()
{
    Fixture f;
    static_cast<void>(f.rotate());
    f.reopen();
    CHECK(f.send(new_owner) == BackendAdmission::executed);
}
void retry_after_lost_ack_is_idempotent()
{
    Fixture f;
    f.ack.fail = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    f.reopen();
    CHECK(f.rotate() == HandoffDisposition::already_durable);
    CHECK_EQ(f.ack.calls, 1);
}
void write_failure_quarantines()
{
    Fixture f;
    f.epoch_hook.fail_next = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    CHECK(f.gate->quarantined());
}
void quarantine_blocks_current_owner()
{
    Fixture f;
    f.epoch_hook.fail_next = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    CHECK(f.send(old_owner) == BackendAdmission::rejected_quarantined);
    CHECK_EQ(f.sink.effects, 0);
}
void quarantine_blocks_candidate_owner()
{
    Fixture f;
    f.epoch_hook.fail_next = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    CHECK(f.send(new_owner) == BackendAdmission::rejected_quarantined);
}
void quarantine_refuses_new_handoff()
{
    Fixture f;
    f.epoch_hook.fail_next = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    f.epoch_hook.fail_next = false;
    CHECK_THROWS_AS(f.rotate(), std::logic_error);
}
void known_prewrite_failure_can_reopen()
{
    Fixture f;
    f.epoch_hook.fail_next = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    f.epoch_hook.fail_next = false;
    f.reopen();
    CHECK_EQ(f.ledger->current().value, old_owner.value);
    CHECK(f.send(old_owner) == BackendAdmission::executed);
}
void independent_gate_has_independent_authority()
{
    Fixture first;
    Fixture second;
    static_cast<void>(first.rotate());
    CHECK(first.send(new_owner) == BackendAdmission::executed);
    CHECK(second.send(old_owner) == BackendAdmission::executed);
}
void backend_exception_propagates()
{
    Fixture f;
    f.sink.on_execute = [](FencedBackendCommand) { throw std::runtime_error{"effect failed"}; };
    CHECK_THROWS_AS(f.send(old_owner), std::runtime_error);
}
void backend_exception_does_not_poison_epoch()
{
    Fixture f;
    f.sink.on_execute = [](FencedBackendCommand) { throw std::runtime_error{"effect unknown"}; };
    CHECK_THROWS_AS(f.send(old_owner), std::runtime_error);
    CHECK(!f.gate->quarantined());
    f.sink.on_execute = {};
    CHECK(f.send(old_owner, 2) == BackendAdmission::executed);
}
void handoff_waits_for_in_flight_effect()
{
    Fixture f;
    std::mutex signal_mutex;
    std::condition_variable signal;
    bool entered = false;
    bool release = false;
    f.sink.on_execute = [&](FencedBackendCommand)
    {
        std::unique_lock lock{signal_mutex};
        entered = true;
        signal.notify_all();
        signal.wait(lock, [&] { return release; });
    };
    std::atomic<bool> handoff_started = false;
    std::atomic<bool> handoff_finished = false;
    std::thread sender{[&] { static_cast<void>(f.send(old_owner)); }};
    {
        std::unique_lock lock{signal_mutex};
        signal.wait(lock, [&] { return entered; });
    }
    std::thread handoff{[&]
                        {
                            handoff_started.store(true);
                            static_cast<void>(f.rotate());
                            handoff_finished.store(true);
                        }};
    while (!handoff_started.load())
    {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{25});
    CHECK(!handoff_finished.load());
    {
        std::lock_guard lock{signal_mutex};
        release = true;
    }
    signal.notify_all();
    sender.join();
    handoff.join();
    CHECK(handoff_finished.load());
    CHECK_EQ(f.ledger->current().value, new_owner.value);
}
void old_owner_after_waited_handoff_rejected()
{
    Fixture f;
    static_cast<void>(f.send(old_owner));
    static_cast<void>(f.rotate());
    CHECK(f.send(old_owner, 2) == BackendAdmission::rejected_stale);
    CHECK_EQ(f.sink.effects, 1);
}
void repeated_stale_rejections_have_no_effects()
{
    Fixture f;
    static_cast<void>(f.rotate());
    for (int i = 1; i <= 4; ++i)
    {
        CHECK(f.send(old_owner, static_cast<std::uint64_t>(i)) == BackendAdmission::rejected_stale);
    }
    CHECK_EQ(f.sink.effects, 0);
}
void stale_after_restart_cannot_write()
{
    Fixture f;
    static_cast<void>(f.rotate());
    f.reopen();
    static_cast<void>(f.send(old_owner, 51));
    static_cast<void>(f.send(old_owner, 52));
    CHECK_EQ(f.sink.effects, 0);
}
} // namespace

cpp_mastery::test::TestSuite make_runtime_backend_fencing_suite()
{
    cpp_mastery::test::TestSuite suite{"backend-fencing"};
    suite.add("no owner refuses command", no_owner_rejects_commands);
    suite.add("current owner executes", current_owner_executes);
    suite.add("backend receives correlated command", effect_receives_correlated_command);
    suite.add("multiple authorized commands", multiple_authorized_operations);
    suite.add("zero command ID refused", command_zero_id_refused);
    suite.add("zero owner token refused", zero_token_refused);
    suite.add("future token refused", future_token_refused);
    suite.add("handoff durable across restart", handoff_is_durable);
    suite.add("ack occurs after persistence", acknowledgement_after_durable_write);
    suite.add("same epoch handoff is replay", equal_epoch_replays);
    suite.add("replay does not duplicate ack", replay_does_not_repeat_acknowledgement);
    suite.add("older epoch handoff refused", rollback_refused);
    suite.add("zero epoch handoff refused", zero_handoff_refused);
    suite.add("epoch skip allowed", epoch_skips_allowed);
    suite.add("old owner refused after handoff", stale_owner_after_handoff_refused);
    suite.add("multiple old owners refused", stale_owner_after_multiple_handoffs_refused);
    suite.add("stale command has no effect", stale_request_has_no_effect);
    suite.add("new owner admitted", replacement_owner_admitted);
    suite.add("future owner cannot self-promote", higher_token_not_self_authorizing);
    suite.add("future token admitted after handoff", future_token_waits_for_handoff);
    suite.add("lost ack retains durable epoch", lost_ack_still_persisted);
    suite.add("lost ack blocks old owner", lost_ack_blocks_old_owner);
    suite.add("reopen refuses old token", restart_refuses_old_token);
    suite.add("reopen admits current token", restart_admits_new_token);
    suite.add("lost ack replay idempotent", retry_after_lost_ack_is_idempotent);
    suite.add("uncertain write quarantines gate", write_failure_quarantines);
    suite.add("quarantine rejects previous owner", quarantine_blocks_current_owner);
    suite.add("quarantine rejects candidate owner", quarantine_blocks_candidate_owner);
    suite.add("quarantine refuses new handoff", quarantine_refuses_new_handoff);
    suite.add("prewrite failure recovery on reopen", known_prewrite_failure_can_reopen);
    suite.add("independent backends", independent_gate_has_independent_authority);
    suite.add("backend exception propagates", backend_exception_propagates);
    suite.add("backend exception does not change epoch", backend_exception_does_not_poison_epoch);
    suite.add("handoff waits for in-flight effect", handoff_waits_for_in_flight_effect);
    suite.add("old owner refused after waited handoff", old_owner_after_waited_handoff_rejected);
    suite.add("repeated stale commands have no effects", repeated_stale_rejections_have_no_effects);
    suite.add("restarted backend rejects stale effects", stale_after_restart_cannot_write);
    return suite;
}
