#include <cpp_mastery/runtime/FencedActivationDispatch.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{
using cpp_mastery::config::EngineConfig;
using namespace cpp_mastery::runtime;

constexpr ActivationAttemptId attempt{1042};
constexpr BackendGeneration generation{7};
constexpr EngineConfig settings{4, 8};
constexpr AuthorityEpoch owner{7};
constexpr AuthorityEpoch next_owner{8};

class TempDirectory final
{
  public:
    TempDirectory()
    {
        std::array<char, 64> name{};
        const std::string pattern = "/tmp/cpp-mastery-l21-XXXXXX";
        std::copy(pattern.begin(), pattern.end(), name.begin());
        if (::mkdtemp(name.data()) == nullptr)
        {
            throw std::runtime_error{"could not create temporary Lesson 21 directory"};
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
    [[nodiscard]] std::filesystem::path journal_file() const
    {
        return path_ / "activation.journal";
    }

  private:
    std::filesystem::path path_;
};

class Sink final : public BackendCommandSink
{
  public:
    int effects = 0;
    std::vector<std::uint64_t> ids;
    bool throw_after_effect = false;
    bool saw_intent = false;
    std::filesystem::path inspect_path;
    void execute(const FencedBackendCommand command) override
    {
        ++effects;
        ids.push_back(command.command_id);
        if (!inspect_path.empty())
        {
            std::ifstream file{inspect_path};
            const std::string contents{std::istreambuf_iterator<char>{file},
                                       std::istreambuf_iterator<char>{}};
            saw_intent = contents.find("AJ1|I|" + std::to_string(command.command_id) + "|") !=
                         std::string::npos;
        }
        if (throw_after_effect)
        {
            throw std::runtime_error{"simulated response lost after external effect"};
        }
    }
};

class JournalHook final : public JournalWriteHook
{
  public:
    char fail_on = '\0';
    int intents = 0;
    int terminals = 0;
    void before_append(const char kind) override
    {
        if (kind == 'I')
        {
            ++intents;
        }
        else
        {
            ++terminals;
        }
        if (kind == fail_on)
        {
            throw std::runtime_error{"journal failpoint"};
        }
    }
};

class EpochHook final : public EpochWriteHook
{
  public:
    bool fail = false;
    void before_append(AuthorityEpoch) override
    {
        if (fail)
        {
            throw std::runtime_error{"epoch write failpoint"};
        }
    }
};

struct Fixture
{
    TempDirectory temp;
    Sink sink;
    JournalHook journal_hook;
    EpochHook epoch_hook;
    std::unique_ptr<AuthorityEpochLedger> epochs;
    std::unique_ptr<FencedBackendGate> gate;
    std::unique_ptr<DurableActivationJournal> journal;
    std::unique_ptr<FencedActivationDispatcher> dispatcher;

    explicit Fixture(const bool initial_owner = true)
    {
        reopen();
        if (initial_owner)
        {
            epochs->advance(owner);
        }
    }
    void reopen()
    {
        dispatcher.reset();
        journal.reset();
        gate.reset();
        epochs.reset();
        epochs = std::make_unique<AuthorityEpochLedger>(temp.epoch_file(), &epoch_hook);
        gate = std::make_unique<FencedBackendGate>(*epochs);
        journal = std::make_unique<DurableActivationJournal>(temp.journal_file(), &journal_hook);
        dispatcher = std::make_unique<FencedActivationDispatcher>(*journal, *gate);
    }
    [[nodiscard]] JournalActivationResult send(ActivationAttemptId id = attempt,
                                               AuthorityEpoch epoch = owner,
                                               BackendGeneration gen = generation,
                                               const EngineConfig& config = settings)
    {
        return dispatcher->dispatch(id, gen, config, epoch, sink);
    }
    [[nodiscard]] JournalActivationState state(ActivationAttemptId id = attempt) const
    {
        return journal->lookup(id).state;
    }
};

void fresh_dispatch_records_started()
{
    Fixture f;
    CHECK(f.send().state == JournalActivationState::started);
    CHECK_EQ(f.sink.effects, 1);
}
void intent_persisted_before_effect()
{
    Fixture f;
    f.sink.inspect_path = f.temp.journal_file();
    static_cast<void>(f.send());
    CHECK(f.sink.saw_intent);
}
void command_identity_correlated()
{
    Fixture f;
    static_cast<void>(f.send({1048}));
    CHECK_EQ(f.sink.ids.size(), 1U);
    CHECK_EQ(f.sink.ids.at(0), 1048U);
}
void journal_records_generation()
{
    Fixture f;
    static_cast<void>(f.send());
    CHECK_EQ(f.journal->lookup(attempt).requested_generation.value, generation.value);
}
void journal_writes_intent_once()
{
    Fixture f;
    static_cast<void>(f.send());
    CHECK_EQ(f.journal_hook.intents, 1);
}
void known_started_record_is_durable()
{
    Fixture f;
    static_cast<void>(f.send());
    f.reopen();
    CHECK(f.state() == JournalActivationState::started);
}
void replay_started_is_read_only()
{
    Fixture f;
    static_cast<void>(f.send());
    CHECK(f.send().state == JournalActivationState::started);
    CHECK_EQ(f.sink.effects, 1);
}
void replay_started_after_reopen_is_read_only()
{
    Fixture f;
    static_cast<void>(f.send());
    f.reopen();
    static_cast<void>(f.send());
    CHECK_EQ(f.sink.effects, 1);
}
void replay_started_after_handoff_is_read_only()
{
    Fixture f;
    static_cast<void>(f.send());
    static_cast<void>(f.gate->handoff(next_owner));
    CHECK(f.send(attempt, next_owner).state == JournalActivationState::started);
    CHECK_EQ(f.sink.effects, 1);
}
void exception_after_effect_is_uncertain()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    CHECK(f.send().state == JournalActivationState::pending);
    CHECK_EQ(f.sink.effects, 1);
}
void unknown_replay_never_duplicates()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    static_cast<void>(f.send());
    f.sink.throw_after_effect = false;
    CHECK(f.send().state == JournalActivationState::pending);
    CHECK_EQ(f.sink.effects, 1);
}
void unknown_survives_restart_without_retry()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    static_cast<void>(f.send());
    f.reopen();
    f.sink.throw_after_effect = false;
    CHECK(f.send().state == JournalActivationState::pending);
    CHECK_EQ(f.sink.effects, 1);
}
void unknown_survives_handoff_without_retry()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    static_cast<void>(f.send());
    static_cast<void>(f.gate->handoff(next_owner));
    f.sink.throw_after_effect = false;
    CHECK(f.send(attempt, next_owner).state == JournalActivationState::pending);
    CHECK_EQ(f.sink.effects, 1);
}
void repeated_uncertain_replays_do_not_execute()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    static_cast<void>(f.send());
    f.sink.throw_after_effect = false;
    for (int i = 0; i < 4; ++i)
    {
        CHECK(f.send().state == JournalActivationState::pending);
    }
    CHECK_EQ(f.sink.effects, 1);
}
void pending_blocks_new_id_without_side_effect()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    static_cast<void>(f.send());
    f.sink.throw_after_effect = false;
    CHECK_THROWS_AS(f.send({1043}), std::logic_error);
    CHECK_EQ(f.sink.effects, 1);
}
void mismatch_generation_does_not_dispatch()
{
    Fixture f;
    static_cast<void>(f.send());
    CHECK_THROWS_AS(f.send(attempt, owner, {10}), std::invalid_argument);
    CHECK_EQ(f.sink.effects, 1);
}
void mismatch_settings_does_not_dispatch()
{
    Fixture f;
    static_cast<void>(f.send());
    CHECK_THROWS_AS(f.send(attempt, owner, generation, EngineConfig{8, 8}), std::invalid_argument);
    CHECK_EQ(f.sink.effects, 1);
}
void zero_id_refused_before_effect()
{
    Fixture f;
    CHECK_THROWS_AS(f.send({0}), std::invalid_argument);
    CHECK_EQ(f.sink.effects, 0);
}
void zero_generation_refused_before_effect()
{
    Fixture f;
    CHECK_THROWS_AS(f.send(attempt, owner, {0}), std::invalid_argument);
    CHECK_EQ(f.sink.effects, 0);
}
void invalid_config_refused_before_effect()
{
    Fixture f;
    CHECK_THROWS_AS(f.send(attempt, owner, generation, EngineConfig{0, 8}), std::invalid_argument);
    CHECK_EQ(f.sink.effects, 0);
}
void no_authority_proves_not_started()
{
    Fixture f{false};
    CHECK(f.send().state == JournalActivationState::not_started);
    CHECK_EQ(f.sink.effects, 0);
}
void old_owner_rejection_is_terminal()
{
    Fixture f;
    static_cast<void>(f.gate->handoff(next_owner));
    CHECK(f.send().state == JournalActivationState::not_started);
    CHECK_EQ(f.sink.effects, 0);
}
void fresh_owner_can_dispatch_after_handoff()
{
    Fixture f;
    static_cast<void>(f.gate->handoff(next_owner));
    CHECK(f.send(attempt, next_owner).state == JournalActivationState::started);
    CHECK_EQ(f.sink.effects, 1);
}
void terminal_stale_replay_has_no_effect()
{
    Fixture f;
    static_cast<void>(f.send(attempt, {6}));
    static_cast<void>(f.send(attempt, owner));
    CHECK_EQ(f.sink.effects, 0);
}
void terminal_no_start_allows_new_id()
{
    Fixture f;
    static_cast<void>(f.gate->handoff(next_owner));
    static_cast<void>(f.send());
    CHECK(f.send({1043}, next_owner).state == JournalActivationState::started);
    CHECK_EQ(f.sink.effects, 1);
}
void pre_intent_failure_prevents_dispatch()
{
    Fixture f;
    f.journal_hook.fail_on = 'I';
    CHECK_THROWS_AS(f.send(), std::runtime_error);
    CHECK_EQ(f.sink.effects, 0);
}
void terminal_failure_preserves_pending()
{
    Fixture f;
    f.journal_hook.fail_on = 'S';
    CHECK_THROWS_AS(f.send(), std::runtime_error);
    CHECK(f.state() == JournalActivationState::pending);
    CHECK_EQ(f.sink.effects, 1);
}
void pending_after_terminal_failure_does_not_duplicate()
{
    Fixture f;
    f.journal_hook.fail_on = 'S';
    CHECK_THROWS_AS(f.send(), std::runtime_error);
    f.journal_hook.fail_on = '\0';
    CHECK(f.send().state == JournalActivationState::pending);
    CHECK_EQ(f.sink.effects, 1);
}
void confirmed_pending_reply_stops_replay()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    static_cast<void>(f.send());
    static_cast<void>(f.journal->confirm(attempt, ActivationResolution::confirmed_started));
    f.sink.throw_after_effect = false;
    CHECK(f.send().state == JournalActivationState::started);
    CHECK_EQ(f.sink.effects, 1);
}
void confirmed_no_start_allows_new_attempt()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    static_cast<void>(f.send());
    static_cast<void>(f.journal->confirm(attempt, ActivationResolution::confirmed_not_started));
    f.sink.throw_after_effect = false;
    CHECK(f.send({1043}).state == JournalActivationState::started);
    CHECK_EQ(f.sink.effects, 2);
}
void replay_pending_does_not_write_terminal()
{
    Fixture f;
    f.sink.throw_after_effect = true;
    static_cast<void>(f.send());
    const auto before = f.journal_hook.terminals;
    f.sink.throw_after_effect = false;
    static_cast<void>(f.send());
    CHECK_EQ(f.journal_hook.terminals, before);
}
} // namespace

cpp_mastery::test::TestSuite make_runtime_fenced_dispatch_suite()
{
    cpp_mastery::test::TestSuite suite{"fenced-dispatch"};
    suite.add("fresh dispatch records started", fresh_dispatch_records_started);
    suite.add("intent persisted before backend effect", intent_persisted_before_effect);
    suite.add("backend command uses stable attempt ID", command_identity_correlated);
    suite.add("journal retains backend generation", journal_records_generation);
    suite.add("intent written once", journal_writes_intent_once);
    suite.add("known result durable after reopen", known_started_record_is_durable);
    suite.add("terminal replay is read-only", replay_started_is_read_only);
    suite.add("terminal replay after reopen is read-only",
              replay_started_after_reopen_is_read_only);
    suite.add("terminal replay after handoff is read-only",
              replay_started_after_handoff_is_read_only);
    suite.add("lost response leaves pending", exception_after_effect_is_uncertain);
    suite.add("lost response replay never duplicates", unknown_replay_never_duplicates);
    suite.add("lost response restart never duplicates", unknown_survives_restart_without_retry);
    suite.add("lost response handoff never duplicates", unknown_survives_handoff_without_retry);
    suite.add("multiple uncertain replays never duplicate",
              repeated_uncertain_replays_do_not_execute);
    suite.add("pending blocks new ID before effect", pending_blocks_new_id_without_side_effect);
    suite.add("generation mismatch never sends command", mismatch_generation_does_not_dispatch);
    suite.add("configuration mismatch never sends command", mismatch_settings_does_not_dispatch);
    suite.add("zero attempt ID rejected before effect", zero_id_refused_before_effect);
    suite.add("zero generation rejected before effect", zero_generation_refused_before_effect);
    suite.add("invalid config rejected before effect", invalid_config_refused_before_effect);
    suite.add("missing ownership yields no start", no_authority_proves_not_started);
    suite.add("stale owner yields no start", old_owner_rejection_is_terminal);
    suite.add("new owner admitted after handoff", fresh_owner_can_dispatch_after_handoff);
    suite.add("stale replay never has an effect", terminal_stale_replay_has_no_effect);
    suite.add("known no start allows new attempt", terminal_no_start_allows_new_id);
    suite.add("intent write failure stops dispatch", pre_intent_failure_prevents_dispatch);
    suite.add("terminal write failure stays pending", terminal_failure_preserves_pending);
    suite.add("failed terminal retry cannot duplicate",
              pending_after_terminal_failure_does_not_duplicate);
    suite.add("independent confirmation ends pending", confirmed_pending_reply_stops_replay);
    suite.add("confirmed no start permits new ID", confirmed_no_start_allows_new_attempt);
    suite.add("uncertain replay skips terminal write", replay_pending_does_not_write_terminal);
    return suite;
}
