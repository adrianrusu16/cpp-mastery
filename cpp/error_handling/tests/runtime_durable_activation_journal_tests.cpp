#include <cpp_mastery/runtime/DurableActivationJournal.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

namespace
{
using cpp_mastery::config::EngineConfig;
using cpp_mastery::runtime::ActivationAttemptId;
using cpp_mastery::runtime::ActivationOutcome;
using cpp_mastery::runtime::ActivationResolution;
using cpp_mastery::runtime::BackendGeneration;
using cpp_mastery::runtime::DurableActivationJournal;
using cpp_mastery::runtime::JournalActivationBackend;
using cpp_mastery::runtime::JournalActivationState;
using cpp_mastery::runtime::JournalWriteHook;

constexpr EngineConfig config{4, 8};
constexpr BackendGeneration generation{7};
constexpr ActivationAttemptId attempt{1042};

class TempDirectory final
{
  public:
    TempDirectory()
    {
        std::array<char, 64> pattern{};
        const std::string prefix = "/tmp/cpp-mastery-l17-XXXXXX";
        std::copy(prefix.begin(), prefix.end(), pattern.begin());
        if (::mkdtemp(pattern.data()) == nullptr)
        {
            throw std::runtime_error{"cannot create temporary journal directory"};
        }
        path_ = pattern.data();
    }
    ~TempDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    [[nodiscard]] std::filesystem::path file() const
    {
        return path_ / "activation.journal";
    }

  private:
    std::filesystem::path path_;
};

class Backend final : public JournalActivationBackend
{
  public:
    ActivationOutcome outcome = ActivationOutcome::unknown;
    bool throws = false;
    int calls = 0;
    bool saw_intent = false;
    std::filesystem::path inspect_path;

    [[nodiscard]] ActivationOutcome activate(ActivationAttemptId id, BackendGeneration,
                                             const EngineConfig&) override
    {
        ++calls;
        if (!inspect_path.empty())
        {
            std::ifstream file{inspect_path};
            const std::string contents{std::istreambuf_iterator<char>{file},
                                       std::istreambuf_iterator<char>{}};
            saw_intent =
                contents.find("AJ1|I|" + std::to_string(id.value) + "|") != std::string::npos;
        }
        if (throws)
        {
            throw std::runtime_error{"backend outcome was lost"};
        }
        return outcome;
    }
};

class Hook final : public JournalWriteHook
{
  public:
    char fail_on = '\0';
    int intent_calls = 0;
    int terminal_calls = 0;
    void before_append(const char kind) override
    {
        if (kind == 'I')
        {
            ++intent_calls;
        }
        else
        {
            ++terminal_calls;
        }
        if (kind == fail_on)
        {
            throw std::runtime_error{"injected journal write failure"};
        }
    }
};

void journal_new_file_starts_empty()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    CHECK_EQ(journal.size(), 0U);
    CHECK(std::filesystem::exists(temp.file()));
}

void journal_missing_directory_refused()
{
    TempDirectory temp;
    CHECK_THROWS_AS(DurableActivationJournal{temp.file().parent_path() / "missing" / "a"},
                    std::runtime_error);
}

void journal_rejects_zero_identity()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    CHECK_THROWS_AS(journal.begin_or_replay({0}, generation, config, backend),
                    std::invalid_argument);
    CHECK_EQ(backend.calls, 0);
    CHECK_EQ(journal.size(), 0U);
}

void journal_rejects_zero_generation()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    CHECK_THROWS_AS(journal.begin_or_replay(attempt, {0}, config, backend), std::invalid_argument);
    CHECK_EQ(backend.calls, 0);
}

void journal_rejects_invalid_configuration()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    CHECK_THROWS_AS(journal.begin_or_replay(attempt, generation, EngineConfig{0, 8}, backend),
                    std::invalid_argument);
    CHECK_EQ(backend.calls, 0);
}

void intent_is_durable_before_backend()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    backend.inspect_path = temp.file();
    backend.outcome = ActivationOutcome::started;
    const auto result = journal.begin_or_replay(attempt, generation, config, backend);
    CHECK(backend.saw_intent);
    CHECK(result.state == JournalActivationState::started);
}

void no_start_also_observes_intent()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    backend.inspect_path = temp.file();
    backend.outcome = ActivationOutcome::not_started;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    CHECK(backend.saw_intent);
}

void unknown_backend_observes_intent()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    backend.inspect_path = temp.file();
    const auto result = journal.begin_or_replay(attempt, generation, config, backend);
    CHECK(backend.saw_intent);
    CHECK(result.state == JournalActivationState::pending);
}

void backend_throw_observes_intent()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    backend.inspect_path = temp.file();
    backend.throws = true;
    const auto result = journal.begin_or_replay(attempt, generation, config, backend);
    CHECK(backend.saw_intent);
    CHECK(result.state == JournalActivationState::pending);
}

void accepted_is_terminal()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    backend.outcome = ActivationOutcome::started;
    const auto result = journal.begin_or_replay(attempt, generation, config, backend);
    CHECK(result.state == JournalActivationState::started);
    CHECK_EQ(result.requested_generation.value, 7U);
    CHECK_EQ(backend.calls, 1);
}

void confirmed_rejection_is_terminal()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    backend.outcome = ActivationOutcome::not_started;
    CHECK(journal.begin_or_replay(attempt, generation, config, backend).state ==
          JournalActivationState::not_started);
}

void unknown_remains_pending()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    CHECK(journal.begin_or_replay(attempt, generation, config, backend).state ==
          JournalActivationState::pending);
    CHECK_EQ(journal.size(), 1U);
}

void throwing_backend_remains_pending()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    backend.throws = true;
    CHECK(journal.begin_or_replay(attempt, generation, config, backend).state ==
          JournalActivationState::pending);
    CHECK_EQ(backend.calls, 1);
}

void pending_replay_does_not_activate()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    backend.outcome = ActivationOutcome::started;
    const auto replay = journal.begin_or_replay(attempt, generation, config, backend);
    CHECK(replay.state == JournalActivationState::pending);
    CHECK_EQ(backend.calls, 1);
}

void pending_replay_after_restart_does_not_activate()
{
    TempDirectory temp;
    Backend backend;
    {
        DurableActivationJournal first{temp.file()};
        static_cast<void>(first.begin_or_replay(attempt, generation, config, backend));
    }
    DurableActivationJournal reopened{temp.file()};
    backend.outcome = ActivationOutcome::started;
    CHECK(reopened.begin_or_replay(attempt, generation, config, backend).state ==
          JournalActivationState::pending);
    CHECK_EQ(backend.calls, 1);
}

void throwing_backend_restart_does_not_retry()
{
    TempDirectory temp;
    Backend backend;
    backend.throws = true;
    {
        DurableActivationJournal first{temp.file()};
        static_cast<void>(first.begin_or_replay(attempt, generation, config, backend));
    }
    DurableActivationJournal reopened{temp.file()};
    backend.throws = false;
    backend.outcome = ActivationOutcome::started;
    CHECK(reopened.begin_or_replay(attempt, generation, config, backend).state ==
          JournalActivationState::pending);
    CHECK_EQ(backend.calls, 1);
}

void repeated_pending_replay_never_reactivates()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    for (int i = 0; i < 6; ++i)
    {
        CHECK(journal.begin_or_replay(attempt, generation, config, backend).state ==
              JournalActivationState::pending);
    }
    CHECK_EQ(backend.calls, 1);
}

void started_replay_survives_restart()
{
    TempDirectory temp;
    Backend backend;
    backend.outcome = ActivationOutcome::started;
    {
        DurableActivationJournal first{temp.file()};
        static_cast<void>(first.begin_or_replay(attempt, generation, config, backend));
    }
    DurableActivationJournal reopened{temp.file()};
    backend.outcome = ActivationOutcome::not_started;
    CHECK(reopened.begin_or_replay(attempt, generation, config, backend).state ==
          JournalActivationState::started);
    CHECK_EQ(backend.calls, 1);
}

void rejected_replay_survives_restart()
{
    TempDirectory temp;
    Backend backend;
    backend.outcome = ActivationOutcome::not_started;
    {
        DurableActivationJournal first{temp.file()};
        static_cast<void>(first.begin_or_replay(attempt, generation, config, backend));
    }
    DurableActivationJournal reopened{temp.file()};
    backend.outcome = ActivationOutcome::started;
    CHECK(reopened.begin_or_replay(attempt, generation, config, backend).state ==
          JournalActivationState::not_started);
    CHECK_EQ(backend.calls, 1);
}

void pending_conflicting_config_rejected()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    CHECK_THROWS_AS(journal.begin_or_replay(attempt, generation, EngineConfig{8, 8}, backend),
                    std::invalid_argument);
    CHECK_EQ(backend.calls, 1);
}

void pending_conflicting_generation_rejected()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    CHECK_THROWS_AS(journal.begin_or_replay(attempt, {8}, config, backend), std::invalid_argument);
    CHECK_EQ(backend.calls, 1);
}

void new_identity_blocked_while_pending()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    CHECK_THROWS_AS(journal.begin_or_replay({1043}, generation, config, backend), std::logic_error);
    CHECK_EQ(backend.calls, 1);
}

void confirmed_started_after_restart()
{
    TempDirectory temp;
    Backend backend;
    {
        DurableActivationJournal first{temp.file()};
        static_cast<void>(first.begin_or_replay(attempt, generation, config, backend));
    }
    {
        DurableActivationJournal reopened{temp.file()};
        CHECK(reopened.confirm(attempt, ActivationResolution::confirmed_started).state ==
              JournalActivationState::started);
    }
    DurableActivationJournal restored{temp.file()};
    CHECK(restored.lookup(attempt).state == JournalActivationState::started);
    CHECK_EQ(backend.calls, 1);
}

void confirmed_not_started_after_restart()
{
    TempDirectory temp;
    Backend backend;
    {
        DurableActivationJournal first{temp.file()};
        static_cast<void>(first.begin_or_replay(attempt, generation, config, backend));
    }
    {
        DurableActivationJournal reopened{temp.file()};
        CHECK(reopened.confirm(attempt, ActivationResolution::confirmed_not_started).state ==
              JournalActivationState::not_started);
    }
    DurableActivationJournal restored{temp.file()};
    CHECK(restored.lookup(attempt).state == JournalActivationState::not_started);
}

void terminal_confirmation_is_idempotent()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    static_cast<void>(journal.confirm(attempt, ActivationResolution::confirmed_started));
    const auto size_before = std::filesystem::file_size(temp.file());
    CHECK(journal.confirm(attempt, ActivationResolution::confirmed_started).state ==
          JournalActivationState::started);
    CHECK_EQ(std::filesystem::file_size(temp.file()), size_before);
}

void contradictory_confirmation_refused()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    static_cast<void>(journal.confirm(attempt, ActivationResolution::confirmed_started));
    CHECK_THROWS_AS(journal.confirm(attempt, ActivationResolution::confirmed_not_started),
                    std::logic_error);
}

void unknown_confirmation_id_refused()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    CHECK_THROWS_AS(journal.confirm(attempt, ActivationResolution::confirmed_started),
                    std::invalid_argument);
}

void after_confirmed_no_start_new_id_allowed()
{
    TempDirectory temp;
    DurableActivationJournal journal{temp.file()};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    static_cast<void>(journal.confirm(attempt, ActivationResolution::confirmed_not_started));
    backend.outcome = ActivationOutcome::started;
    CHECK(journal.begin_or_replay({1043}, generation, config, backend).state ==
          JournalActivationState::started);
    CHECK_EQ(backend.calls, 2);
}

void write_failure_before_intent_skips_backend()
{
    TempDirectory temp;
    Hook hook;
    hook.fail_on = 'I';
    DurableActivationJournal journal{temp.file(), &hook};
    Backend backend;
    CHECK_THROWS_AS(journal.begin_or_replay(attempt, generation, config, backend),
                    std::runtime_error);
    CHECK_EQ(backend.calls, 0);
    CHECK_EQ(journal.size(), 0U);
}

void write_failure_before_intent_restart_is_empty()
{
    TempDirectory temp;
    Backend backend;
    {
        Hook hook;
        hook.fail_on = 'I';
        DurableActivationJournal journal{temp.file(), &hook};
        CHECK_THROWS_AS(journal.begin_or_replay(attempt, generation, config, backend),
                        std::runtime_error);
    }
    DurableActivationJournal reopened{temp.file()};
    CHECK_EQ(reopened.size(), 0U);
    CHECK_EQ(backend.calls, 0);
}

void terminal_write_failure_restarts_as_pending()
{
    TempDirectory temp;
    Backend backend;
    backend.outcome = ActivationOutcome::started;
    {
        Hook hook;
        hook.fail_on = 'S';
        DurableActivationJournal journal{temp.file(), &hook};
        CHECK_THROWS_AS(journal.begin_or_replay(attempt, generation, config, backend),
                        std::runtime_error);
        CHECK(journal.lookup(attempt).state == JournalActivationState::pending);
    }
    DurableActivationJournal reopened{temp.file()};
    CHECK(reopened.lookup(attempt).state == JournalActivationState::pending);
    backend.outcome = ActivationOutcome::started;
    CHECK(reopened.begin_or_replay(attempt, generation, config, backend).state ==
          JournalActivationState::pending);
    CHECK_EQ(backend.calls, 1);
}

void failed_confirmation_write_can_retry()
{
    TempDirectory temp;
    Hook hook;
    DurableActivationJournal journal{temp.file(), &hook};
    Backend backend;
    static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    hook.fail_on = 'N';
    CHECK_THROWS_AS(journal.confirm(attempt, ActivationResolution::confirmed_not_started),
                    std::runtime_error);
    CHECK(journal.lookup(attempt).state == JournalActivationState::pending);
    hook.fail_on = '\0';
    CHECK(journal.confirm(attempt, ActivationResolution::confirmed_not_started).state ==
          JournalActivationState::not_started);
}

void torn_trailing_record_is_rejected()
{
    TempDirectory temp;
    {
        DurableActivationJournal journal{temp.file()};
        Backend backend;
        static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    }
    std::ofstream tail{temp.file(), std::ios::app};
    tail << "AJ1|N|";
    tail.close();
    CHECK_THROWS_AS(DurableActivationJournal{temp.file()}, std::runtime_error);
}

void checksum_corruption_is_rejected()
{
    TempDirectory temp;
    {
        DurableActivationJournal journal{temp.file()};
        Backend backend;
        static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    }
    {
        std::fstream file{temp.file(), std::ios::in | std::ios::out};
        file.seekp(0);
        file.put('X');
    }
    CHECK_THROWS_AS(DurableActivationJournal{temp.file()}, std::runtime_error);
}

void invalid_transition_is_rejected()
{
    TempDirectory temp;
    {
        DurableActivationJournal journal{temp.file()};
        Backend backend;
        backend.outcome = ActivationOutcome::started;
        static_cast<void>(journal.begin_or_replay(attempt, generation, config, backend));
    }
    std::ifstream input{temp.file()};
    std::string first;
    std::string second;
    std::getline(input, first);
    std::getline(input, second);
    input.close();
    std::ofstream output{temp.file(), std::ios::trunc};
    output << second << '\n';
    output.close();
    CHECK_THROWS_AS(DurableActivationJournal{temp.file()}, std::runtime_error);
}

void second_writer_is_refused()
{
    TempDirectory temp;
    DurableActivationJournal first{temp.file()};
    CHECK_THROWS_AS(DurableActivationJournal{temp.file()}, std::runtime_error);
}

void another_file_is_independent()
{
    TempDirectory temp;
    DurableActivationJournal first{temp.file()};
    DurableActivationJournal second{temp.file().parent_path() / "other.journal"};
    Backend first_backend;
    Backend second_backend;
    static_cast<void>(first.begin_or_replay(attempt, generation, config, first_backend));
    CHECK_EQ(second.size(), 0U);
    CHECK(second.begin_or_replay(attempt, generation, config, second_backend).state ==
          JournalActivationState::pending);
    CHECK_EQ(first_backend.calls, 1);
    CHECK_EQ(second_backend.calls, 1);
}

void multiple_terminal_history_survives_restart()
{
    TempDirectory temp;
    Backend backend;
    backend.outcome = ActivationOutcome::not_started;
    {
        DurableActivationJournal first{temp.file()};
        static_cast<void>(first.begin_or_replay(attempt, generation, config, backend));
        backend.outcome = ActivationOutcome::started;
        static_cast<void>(first.begin_or_replay({1043}, {8}, EngineConfig{8, 16}, backend));
    }
    DurableActivationJournal reopened{temp.file()};
    CHECK_EQ(reopened.size(), 2U);
    CHECK(reopened.lookup(attempt).state == JournalActivationState::not_started);
    CHECK(reopened.lookup({1043}).state == JournalActivationState::started);
    CHECK_EQ(reopened.lookup({1043}).requested_generation.value, 8U);
}

void pending_replay_after_terminal_history()
{
    TempDirectory temp;
    Backend backend;
    backend.outcome = ActivationOutcome::not_started;
    {
        DurableActivationJournal first{temp.file()};
        static_cast<void>(first.begin_or_replay(attempt, generation, config, backend));
        backend.outcome = ActivationOutcome::unknown;
        static_cast<void>(first.begin_or_replay({1043}, generation, config, backend));
    }
    DurableActivationJournal reopened{temp.file()};
    backend.outcome = ActivationOutcome::started;
    CHECK(reopened.begin_or_replay({1043}, generation, config, backend).state ==
          JournalActivationState::pending);
    CHECK_EQ(backend.calls, 2);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_durable_journal_suite()
{
    cpp_mastery::test::TestSuite suite{"durable-journal"};
    suite.add("new journal starts empty", journal_new_file_starts_empty);
    suite.add("missing directory rejected", journal_missing_directory_refused);
    suite.add("zero identity rejected", journal_rejects_zero_identity);
    suite.add("zero generation rejected", journal_rejects_zero_generation);
    suite.add("invalid configuration rejected", journal_rejects_invalid_configuration);
    suite.add("intent durable before activation", intent_is_durable_before_backend);
    suite.add("no-start observes intent", no_start_also_observes_intent);
    suite.add("unknown observes intent", unknown_backend_observes_intent);
    suite.add("throwing backend observes intent", backend_throw_observes_intent);
    suite.add("accepted journal is terminal", accepted_is_terminal);
    suite.add("confirmed no-start terminal", confirmed_rejection_is_terminal);
    suite.add("unknown stays pending", unknown_remains_pending);
    suite.add("throwing backend pending", throwing_backend_remains_pending);
    suite.add("pending replay read only", pending_replay_does_not_activate);
    suite.add("pending replay after restart", pending_replay_after_restart_does_not_activate);
    suite.add("exception restart does not retry", throwing_backend_restart_does_not_retry);
    suite.add("repeated pending replay", repeated_pending_replay_never_reactivates);
    suite.add("accepted replay after restart", started_replay_survives_restart);
    suite.add("no-start replay after restart", rejected_replay_survives_restart);
    suite.add("pending input conflict", pending_conflicting_config_rejected);
    suite.add("pending generation conflict", pending_conflicting_generation_rejected);
    suite.add("new identity blocked while pending", new_identity_blocked_while_pending);
    suite.add("started resolved after restart", confirmed_started_after_restart);
    suite.add("no-start resolved after restart", confirmed_not_started_after_restart);
    suite.add("terminal resolution idempotent", terminal_confirmation_is_idempotent);
    suite.add("contradictory resolution rejected", contradictory_confirmation_refused);
    suite.add("unknown resolution rejected", unknown_confirmation_id_refused);
    suite.add("new identity after no-start", after_confirmed_no_start_new_id_allowed);
    suite.add("intent I/O failure skips backend", write_failure_before_intent_skips_backend);
    suite.add("intent I/O failure restart empty", write_failure_before_intent_restart_is_empty);
    suite.add("terminal I/O failure replays pending", terminal_write_failure_restarts_as_pending);
    suite.add("failed confirmation can retry", failed_confirmation_write_can_retry);
    suite.add("torn record rejected", torn_trailing_record_is_rejected);
    suite.add("checksum corruption rejected", checksum_corruption_is_rejected);
    suite.add("invalid transition rejected", invalid_transition_is_rejected);
    suite.add("second writer rejected", second_writer_is_refused);
    suite.add("independent journal files", another_file_is_independent);
    suite.add("terminal history survives restart", multiple_terminal_history_survives_restart);
    suite.add("pending after terminal history", pending_replay_after_terminal_history);
    return suite;
}
