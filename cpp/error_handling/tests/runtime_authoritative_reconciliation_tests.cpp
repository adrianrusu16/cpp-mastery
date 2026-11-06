#include <cpp_mastery/runtime/AuthoritativeActivationReconciler.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace
{
using cpp_mastery::config::EngineConfig;
using namespace cpp_mastery::runtime;

constexpr ActivationAttemptId attempt{1042};
constexpr BackendGeneration generation{7};
constexpr EngineConfig settings{4, 8};

class TempDirectory final
{
  public:
    TempDirectory()
    {
        std::array<char, 64> name{};
        const std::string pattern = "/tmp/cpp-mastery-l18-XXXXXX";
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
    int calls = 0;
    [[nodiscard]] ActivationOutcome activate(ActivationAttemptId, BackendGeneration,
                                             const EngineConfig&) override
    {
        ++calls;
        return outcome;
    }
};

class Authority final : public ActivationStatusAuthority
{
  public:
    AuthoritativeActivationStatus status = AuthoritativeActivationStatus::unavailable;
    int calls = 0;
    bool throws = false;
    bool wrong_id = false;
    bool wrong_generation = false;
    ActivationAttemptId seen_id{0};
    BackendGeneration seen_generation{0};

    [[nodiscard]] ActivationStatusReply query(const ActivationAttemptId id,
                                              const BackendGeneration gen) override
    {
        ++calls;
        seen_id = id;
        seen_generation = gen;
        if (throws)
        {
            throw std::runtime_error{"authority temporarily unreachable"};
        }
        return {{wrong_id ? id.value + 1 : id.value},
                {wrong_generation ? gen.value + 1 : gen.value},
                status};
    }
};

class Hook final : public JournalWriteHook
{
  public:
    char fail_on = '\0';
    int terminal_calls = 0;
    void before_append(const char kind) override
    {
        if (kind != 'I')
        {
            ++terminal_calls;
        }
        if (kind == fail_on)
        {
            throw std::runtime_error{"injected terminal write failure"};
        }
    }
};

struct Fixture
{
    TempDirectory temp;
    Backend backend;
    Authority authority;
    Hook hook;
    std::unique_ptr<DurableActivationJournal> journal;
    std::unique_ptr<AuthoritativeActivationReconciler> reconciler;

    explicit Fixture(const ActivationOutcome initial = ActivationOutcome::unknown)
    {
        backend.outcome = initial;
        reopen();
        static_cast<void>(journal->begin_or_replay(attempt, generation, settings, backend));
    }

    void reopen()
    {
        reconciler.reset();
        journal.reset();
        journal = std::make_unique<DurableActivationJournal>(temp.file(), &hook);
        reconciler = std::make_unique<AuthoritativeActivationReconciler>(*journal);
    }

    [[nodiscard]] ReconciliationResult reconcile()
    {
        return reconciler->reconcile(attempt, authority);
    }

    [[nodiscard]] JournalActivationState state() const
    {
        return journal->lookup(attempt).state;
    }
};

void journal_pending_at_start()
{
    Fixture f;
    CHECK(f.state() == JournalActivationState::pending);
    CHECK_EQ(f.backend.calls, 1);
}

void correct_query_identity_and_generation()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.authority.seen_id.value, attempt.value);
    CHECK_EQ(f.authority.seen_generation.value, generation.value);
}

void unavailable_remains_pending()
{
    Fixture f;
    const auto r = f.reconcile();
    CHECK(r.queried_authority);
    CHECK(r.journal.state == JournalActivationState::pending);
    CHECK(f.state() == JournalActivationState::pending);
}

void unavailable_survives_restart()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    f.reopen();
    CHECK(f.state() == JournalActivationState::pending);
}

void unavailable_never_reactivates()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.backend.calls, 1);
}

void unavailable_blocks_new_identity()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    CHECK_THROWS_AS(f.journal->begin_or_replay({1043}, generation, settings, f.backend),
                    std::logic_error);
    CHECK_EQ(f.backend.calls, 1);
}

void unavailable_can_be_polled_again()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    static_cast<void>(f.reconcile());
    CHECK(f.state() == JournalActivationState::pending);
    CHECK_EQ(f.authority.calls, 2);
}

void repeated_unavailable_never_finishes()
{
    Fixture f;
    for (int i = 0; i < 5; ++i)
    {
        CHECK(f.reconcile().journal.state == JournalActivationState::pending);
    }
    CHECK_EQ(f.backend.calls, 1);
}

void unavailable_then_started()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    f.authority.status = AuthoritativeActivationStatus::started;
    CHECK(f.reconcile().journal.state == JournalActivationState::started);
    CHECK_EQ(f.backend.calls, 1);
}

void unavailable_then_not_started()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    f.authority.status = AuthoritativeActivationStatus::not_started;
    CHECK(f.reconcile().journal.state == JournalActivationState::not_started);
}

void throwing_authority_keeps_pending()
{
    Fixture f;
    f.authority.throws = true;
    const auto result = f.reconcile();
    CHECK(result.queried_authority);
    CHECK(result.journal.state == JournalActivationState::pending);
    CHECK(f.state() == JournalActivationState::pending);
}

void throwing_authority_does_not_activate()
{
    Fixture f;
    f.authority.throws = true;
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.backend.calls, 1);
}

void throwing_authority_still_blocks_next_id()
{
    Fixture f;
    f.authority.throws = true;
    static_cast<void>(f.reconcile());
    CHECK_THROWS_AS(f.journal->begin_or_replay({1043}, generation, settings, f.backend),
                    std::logic_error);
}

void throwing_authority_survives_restart()
{
    Fixture f;
    f.authority.throws = true;
    static_cast<void>(f.reconcile());
    f.reopen();
    CHECK(f.state() == JournalActivationState::pending);
}

void throwing_authority_can_recover()
{
    Fixture f;
    f.authority.throws = true;
    static_cast<void>(f.reconcile());
    f.authority.throws = false;
    f.authority.status = AuthoritativeActivationStatus::started;
    CHECK(f.reconcile().journal.state == JournalActivationState::started);
}

void query_unavailable_after_restart()
{
    Fixture f;
    f.reopen();
    CHECK(f.reconcile().journal.state == JournalActivationState::pending);
    CHECK_EQ(f.backend.calls, 1);
}

void started_is_durable()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    CHECK(f.reconcile().journal.state == JournalActivationState::started);
    f.reopen();
    CHECK(f.state() == JournalActivationState::started);
}

void not_started_is_durable()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    CHECK(f.reconcile().journal.state == JournalActivationState::not_started);
    f.reopen();
    CHECK(f.state() == JournalActivationState::not_started);
}

void terminal_started_skips_query()
{
    Fixture f{ActivationOutcome::started};
    CHECK(!f.reconcile().queried_authority);
    CHECK_EQ(f.authority.calls, 0);
}

void terminal_not_started_skips_query()
{
    Fixture f{ActivationOutcome::not_started};
    CHECK(!f.reconcile().queried_authority);
    CHECK_EQ(f.authority.calls, 0);
}

void terminal_started_keeps_original_result()
{
    Fixture f{ActivationOutcome::started};
    f.authority.status = AuthoritativeActivationStatus::not_started;
    CHECK(f.reconcile().journal.state == JournalActivationState::started);
    CHECK_EQ(f.authority.calls, 0);
}

void terminal_after_reopen_skips_query()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    static_cast<void>(f.reconcile());
    f.reopen();
    CHECK(!f.reconcile().queried_authority);
    CHECK_EQ(f.authority.calls, 1);
}

void missing_attempt_skips_query()
{
    Fixture f;
    CHECK_THROWS_AS(f.reconciler->reconcile({9999}, f.authority), std::invalid_argument);
    CHECK_EQ(f.authority.calls, 0);
}

void zero_attempt_skips_query()
{
    Fixture f;
    CHECK_THROWS_AS(f.reconciler->reconcile({0}, f.authority), std::invalid_argument);
    CHECK_EQ(f.authority.calls, 0);
}

void started_confirms_once()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    static_cast<void>(f.reconcile());
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.hook.terminal_calls, 1);
    CHECK_EQ(f.authority.calls, 1);
}

void not_started_confirms_once()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    static_cast<void>(f.reconcile());
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.hook.terminal_calls, 1);
    CHECK_EQ(f.authority.calls, 1);
}

void wrong_attempt_rejected()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    f.authority.wrong_id = true;
    CHECK_THROWS_AS(f.reconcile(), std::invalid_argument);
    CHECK(f.state() == JournalActivationState::pending);
}

void wrong_generation_rejected()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    f.authority.wrong_generation = true;
    CHECK_THROWS_AS(f.reconcile(), std::invalid_argument);
    CHECK(f.state() == JournalActivationState::pending);
}

void invalid_status_rejected()
{
    Fixture f;
    f.authority.status = static_cast<AuthoritativeActivationStatus>(99);
    CHECK_THROWS_AS(f.reconcile(), std::invalid_argument);
    CHECK(f.state() == JournalActivationState::pending);
}

void mismatched_reply_no_disk_transition()
{
    Fixture f;
    f.authority.wrong_id = true;
    CHECK_THROWS_AS(f.reconcile(), std::invalid_argument);
    CHECK_EQ(f.hook.terminal_calls, 0);
    f.reopen();
    CHECK(f.state() == JournalActivationState::pending);
}

void confirmed_not_started_allows_new_id()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    static_cast<void>(f.reconcile());
    CHECK(f.journal->begin_or_replay({1043}, generation, settings, f.backend).state ==
          JournalActivationState::pending);
    CHECK_EQ(f.backend.calls, 2);
}

void confirmation_does_not_reactivate_original_id()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    static_cast<void>(f.reconcile());
    CHECK(f.journal->begin_or_replay(attempt, generation, settings, f.backend).state ==
          JournalActivationState::not_started);
    CHECK_EQ(f.backend.calls, 1);
}

void confirmed_started_does_not_reactivate_original_id()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    static_cast<void>(f.reconcile());
    CHECK(f.journal->begin_or_replay(attempt, generation, settings, f.backend).state ==
          JournalActivationState::started);
    CHECK_EQ(f.backend.calls, 1);
}

void terminal_write_failure_stays_pending()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    f.hook.fail_on = 'S';
    CHECK_THROWS_AS(f.reconcile(), std::runtime_error);
    CHECK(f.state() == JournalActivationState::pending);
}

void failed_confirmation_survives_reopen()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    f.hook.fail_on = 'N';
    CHECK_THROWS_AS(f.reconcile(), std::runtime_error);
    f.reopen();
    CHECK(f.state() == JournalActivationState::pending);
}

void failed_confirmation_can_be_retried()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    f.hook.fail_on = 'S';
    CHECK_THROWS_AS(f.reconcile(), std::runtime_error);
    f.hook.fail_on = '\0';
    CHECK(f.reconcile().journal.state == JournalActivationState::started);
    CHECK_EQ(f.authority.calls, 2);
}

void unavailable_does_not_write_terminal()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.hook.terminal_calls, 0);
}

void throwing_authority_does_not_write_terminal()
{
    Fixture f;
    f.authority.throws = true;
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.hook.terminal_calls, 0);
}

void mismatch_prevents_new_attempt()
{
    Fixture f;
    f.authority.wrong_generation = true;
    CHECK_THROWS_AS(f.reconcile(), std::invalid_argument);
    CHECK_THROWS_AS(f.journal->begin_or_replay({1043}, generation, settings, f.backend),
                    std::logic_error);
}

void terminal_history_then_unavailable_pending()
{
    Fixture f{ActivationOutcome::not_started};
    f.backend.outcome = ActivationOutcome::unknown;
    static_cast<void>(f.journal->begin_or_replay({1043}, generation, settings, f.backend));
    f.authority.status = AuthoritativeActivationStatus::unavailable;
    const auto result = f.reconciler->reconcile({1043}, f.authority);
    CHECK(result.journal.state == JournalActivationState::pending);
    CHECK(f.journal->lookup(attempt).state == JournalActivationState::not_started);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_authoritative_reconciliation_suite()
{
    cpp_mastery::test::TestSuite suite{"authoritative-reconciliation"};
    suite.add("journal starts pending", journal_pending_at_start);
    suite.add("queries correlated identity", correct_query_identity_and_generation);
    suite.add("unavailable stays pending", unavailable_remains_pending);
    suite.add("unavailable survives restart", unavailable_survives_restart);
    suite.add("unavailable never reactivates", unavailable_never_reactivates);
    suite.add("unavailable blocks next ID", unavailable_blocks_new_identity);
    suite.add("unavailable may be repolled", unavailable_can_be_polled_again);
    suite.add("repeated unavailable stays pending", repeated_unavailable_never_finishes);
    suite.add("unavailable then started", unavailable_then_started);
    suite.add("unavailable then not started", unavailable_then_not_started);
    suite.add("authority exception stays pending", throwing_authority_keeps_pending);
    suite.add("authority exception no reactivation", throwing_authority_does_not_activate);
    suite.add("exception blocks new ID", throwing_authority_still_blocks_next_id);
    suite.add("exception survives restart", throwing_authority_survives_restart);
    suite.add("exception may be reconciled", throwing_authority_can_recover);
    suite.add("query unavailable after restart", query_unavailable_after_restart);
    suite.add("started outcome is durable", started_is_durable);
    suite.add("not started outcome is durable", not_started_is_durable);
    suite.add("started terminal skips query", terminal_started_skips_query);
    suite.add("not started terminal skips query", terminal_not_started_skips_query);
    suite.add("started terminal cannot be overwritten", terminal_started_keeps_original_result);
    suite.add("terminal replay skips query", terminal_after_reopen_skips_query);
    suite.add("missing ID skips query", missing_attempt_skips_query);
    suite.add("zero ID skips query", zero_attempt_skips_query);
    suite.add("started confirmation once", started_confirms_once);
    suite.add("not started confirmation once", not_started_confirms_once);
    suite.add("wrong attempt reply rejected", wrong_attempt_rejected);
    suite.add("wrong generation reply rejected", wrong_generation_rejected);
    suite.add("invalid status rejected", invalid_status_rejected);
    suite.add("mismatched reply does not persist", mismatched_reply_no_disk_transition);
    suite.add("confirmed no start allows new ID", confirmed_not_started_allows_new_id);
    suite.add("confirmed no start never reactivates ID",
              confirmation_does_not_reactivate_original_id);
    suite.add("started never reactivates ID", confirmed_started_does_not_reactivate_original_id);
    suite.add("terminal write failure stays pending", terminal_write_failure_stays_pending);
    suite.add("failed confirmation survives reopen", failed_confirmation_survives_reopen);
    suite.add("failed confirmation can retry", failed_confirmation_can_be_retried);
    suite.add("unavailable does not write terminal", unavailable_does_not_write_terminal);
    suite.add("exception does not write terminal", throwing_authority_does_not_write_terminal);
    suite.add("mismatch keeps other ID blocked", mismatch_prevents_new_attempt);
    suite.add("terminal history and pending outage", terminal_history_then_unavailable_pending);
    return suite;
}
