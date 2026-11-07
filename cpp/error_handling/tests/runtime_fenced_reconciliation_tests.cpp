#include <cpp_mastery/runtime/FencedActivationReconciler.hpp>
#include <cpp_mastery/testing/Runner.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
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
constexpr BackendGeneration generation{3};
constexpr EngineConfig settings{4, 8};
constexpr AuthorityEpoch first_epoch{7};
constexpr AuthorityEpoch next_epoch{8};

class TempDirectory final
{
  public:
    TempDirectory()
    {
        std::array<char, 64> name{};
        const std::string pattern = "/tmp/cpp-mastery-l19-XXXXXX";
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

    [[nodiscard]] std::filesystem::path journal_file() const
    {
        return path_ / "activation.journal";
    }
    [[nodiscard]] std::filesystem::path epoch_file() const
    {
        return path_ / "authority.epochs";
    }
    [[nodiscard]] std::filesystem::path other_epoch_file() const
    {
        return path_ / "other.epochs";
    }
    [[nodiscard]] std::filesystem::path missing_file() const
    {
        return path_ / "missing" / "epochs";
    }

  private:
    std::filesystem::path path_;
};

class Backend final : public JournalActivationBackend
{
  public:
    int calls = 0;
    [[nodiscard]] ActivationOutcome activate(ActivationAttemptId, BackendGeneration,
                                             const EngineConfig&) override
    {
        ++calls;
        return ActivationOutcome::unknown;
    }
};

class EpochHook final : public EpochWriteHook
{
  public:
    bool throw_next = false;
    int calls = 0;
    AuthorityEpoch last{};
    void before_append(const AuthorityEpoch epoch) override
    {
        ++calls;
        last = epoch;
        if (throw_next)
        {
            throw std::runtime_error{"injected epoch write failure"};
        }
    }
};

class JournalHook final : public JournalWriteHook
{
  public:
    bool throw_terminal = false;
    int terminal_calls = 0;
    void before_append(const char kind) override
    {
        if (kind != 'I')
        {
            ++terminal_calls;
            if (throw_terminal)
            {
                throw std::runtime_error{"injected journal write failure"};
            }
        }
    }
};

class Authority final : public FencedStatusAuthority
{
  public:
    AuthoritativeActivationStatus status = AuthoritativeActivationStatus::unavailable;
    int calls = 0;
    bool throw_query = false;
    bool wrong_id = false;
    bool wrong_generation = false;
    bool use_reply_epoch = false;
    AuthorityEpoch reply_epoch{};
    ActivationAttemptId seen_id{};
    BackendGeneration seen_generation{};
    AuthorityEpoch seen_epoch{};
    std::function<void()> during_query;

    [[nodiscard]] FencedStatusReply query(const ActivationAttemptId id, const BackendGeneration gen,
                                          const AuthorityEpoch epoch) override
    {
        ++calls;
        seen_id = id;
        seen_generation = gen;
        seen_epoch = epoch;
        if (during_query)
        {
            during_query();
        }
        if (throw_query)
        {
            throw std::runtime_error{"authority unavailable"};
        }
        return {{wrong_id ? id.value + 1 : id.value},
                {wrong_generation ? gen.value + 1 : gen.value},
                use_reply_epoch ? reply_epoch : epoch,
                status};
    }
};

struct Fixture
{
    TempDirectory temp;
    Backend backend;
    EpochHook epoch_hook;
    JournalHook journal_hook;
    Authority authority;
    std::unique_ptr<DurableActivationJournal> journal;
    std::unique_ptr<AuthorityEpochLedger> ledger;
    std::unique_ptr<FencedActivationReconciler> reconciler;

    explicit Fixture(const bool initialize_epoch = true)
    {
        reopen();
        if (initialize_epoch)
        {
            ledger->advance(first_epoch);
        }
        static_cast<void>(journal->begin_or_replay(attempt, generation, settings, backend));
    }

    void reopen()
    {
        reconciler.reset();
        ledger.reset();
        journal.reset();
        journal = std::make_unique<DurableActivationJournal>(temp.journal_file(), &journal_hook);
        ledger = std::make_unique<AuthorityEpochLedger>(temp.epoch_file(), &epoch_hook);
        reconciler = std::make_unique<FencedActivationReconciler>(*journal, *ledger);
    }
    [[nodiscard]] FencedReconciliationResult reconcile()
    {
        return reconciler->reconcile(attempt, authority);
    }
    [[nodiscard]] JournalActivationState state() const
    {
        return journal->lookup(attempt).state;
    }
    void rotate()
    {
        ledger->advance(next_epoch);
    }
    void stale(const AuthoritativeActivationStatus status)
    {
        rotate();
        authority.status = status;
        authority.use_reply_epoch = true;
        authority.reply_epoch = first_epoch;
    }
};

void ledger_starts_unfenced()
{
    TempDirectory temp;
    AuthorityEpochLedger ledger{temp.epoch_file()};
    CHECK_EQ(ledger.current().value, 0U);
}

void initial_epoch_persists()
{
    TempDirectory temp;
    {
        AuthorityEpochLedger ledger{temp.epoch_file()};
        ledger.advance(first_epoch);
    }
    AuthorityEpochLedger reopened{temp.epoch_file()};
    CHECK_EQ(reopened.current().value, first_epoch.value);
}

void epoch_rotation_persists()
{
    Fixture f;
    f.rotate();
    f.reopen();
    CHECK_EQ(f.ledger->current().value, next_epoch.value);
}

void epoch_monotonicity()
{
    Fixture f;
    CHECK_THROWS_AS(f.ledger->advance(first_epoch), std::invalid_argument);
    CHECK_THROWS_AS(f.ledger->advance({6}), std::invalid_argument);
    CHECK_THROWS_AS(f.ledger->advance({0}), std::invalid_argument);
    CHECK_EQ(f.ledger->current().value, first_epoch.value);
}

void epoch_can_skip_numbers()
{
    Fixture f;
    f.ledger->advance({11});
    CHECK_EQ(f.ledger->current().value, 11U);
}

void epoch_write_hook_receives_fence()
{
    Fixture f;
    f.rotate();
    CHECK_EQ(f.epoch_hook.calls, 2);
    CHECK_EQ(f.epoch_hook.last.value, next_epoch.value);
}

void failed_epoch_write_preserves_old()
{
    Fixture f;
    f.epoch_hook.throw_next = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    CHECK_EQ(f.ledger->current().value, first_epoch.value);
    f.reopen();
    CHECK_EQ(f.ledger->current().value, first_epoch.value);
}

void failed_epoch_write_can_retry()
{
    Fixture f;
    f.epoch_hook.throw_next = true;
    CHECK_THROWS_AS(f.rotate(), std::runtime_error);
    f.epoch_hook.throw_next = false;
    f.rotate();
    CHECK_EQ(f.ledger->current().value, next_epoch.value);
}

void duplicate_writer_refused()
{
    Fixture f;
    CHECK_THROWS_AS(AuthorityEpochLedger{f.temp.epoch_file()}, std::runtime_error);
}

void independent_epoch_files()
{
    Fixture f;
    AuthorityEpochLedger other{f.temp.other_epoch_file()};
    other.advance({19});
    CHECK_EQ(other.current().value, 19U);
    CHECK_EQ(f.ledger->current().value, first_epoch.value);
}

void missing_directory_refused()
{
    TempDirectory temp;
    CHECK_THROWS_AS(AuthorityEpochLedger{temp.missing_file()}, std::runtime_error);
}

void torn_epoch_record_rejected()
{
    TempDirectory temp;
    {
        AuthorityEpochLedger ledger{temp.epoch_file()};
        ledger.advance(first_epoch);
    }
    {
        std::ofstream out{temp.epoch_file(), std::ios::app};
        out << "FE1|8|";
    }
    CHECK_THROWS_AS(AuthorityEpochLedger{temp.epoch_file()}, std::runtime_error);
}

void corrupted_epoch_record_rejected()
{
    TempDirectory temp;
    {
        AuthorityEpochLedger ledger{temp.epoch_file()};
        ledger.advance(first_epoch);
    }
    {
        std::ofstream out{temp.epoch_file(), std::ios::trunc};
        out << "FE1|7|999\n";
    }
    CHECK_THROWS_AS(AuthorityEpochLedger{temp.epoch_file()}, std::runtime_error);
}

void decreasing_epoch_history_rejected()
{
    TempDirectory temp;
    {
        AuthorityEpochLedger ledger{temp.epoch_file()};
        ledger.advance(first_epoch);
        ledger.advance(next_epoch);
    }
    {
        std::ofstream out{temp.epoch_file(), std::ios::app};
        // Reuse a valid earlier record; checksums alone cannot allow rollback.
        std::ifstream in{temp.epoch_file()};
        std::string first;
        std::getline(in, first);
        out << first << '\n';
    }
    CHECK_THROWS_AS(AuthorityEpochLedger{temp.epoch_file()}, std::runtime_error);
}

void zero_epoch_blocks_reconciliation()
{
    Fixture f{false};
    f.authority.status = AuthoritativeActivationStatus::started;
    CHECK_THROWS_AS(f.reconcile(), std::logic_error);
    CHECK_EQ(f.authority.calls, 0);
    CHECK(f.state() == JournalActivationState::pending);
}

void query_is_fenced()
{
    Fixture f;
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.authority.seen_id.value, attempt.value);
    CHECK_EQ(f.authority.seen_generation.value, generation.value);
    CHECK_EQ(f.authority.seen_epoch.value, first_epoch.value);
}

void current_started_is_durable()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    const auto result = f.reconcile();
    CHECK(result.journal.state == JournalActivationState::started);
    f.reopen();
    CHECK(f.state() == JournalActivationState::started);
}

void current_not_started_is_durable()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    CHECK(f.reconcile().journal.state == JournalActivationState::not_started);
    f.reopen();
    CHECK(f.state() == JournalActivationState::not_started);
}

void current_unavailable_keeps_pending()
{
    Fixture f;
    CHECK(f.reconcile().journal.state == JournalActivationState::pending);
    CHECK(f.state() == JournalActivationState::pending);
    CHECK_EQ(f.journal_hook.terminal_calls, 0);
}

void authority_exception_keeps_pending()
{
    Fixture f;
    f.authority.throw_query = true;
    CHECK(f.reconcile().journal.state == JournalActivationState::pending);
    CHECK_EQ(f.journal_hook.terminal_calls, 0);
}

void stale_started_keeps_pending()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::started);
    const auto result = f.reconcile();
    CHECK(result.rejected_stale);
    CHECK(result.journal.state == JournalActivationState::pending);
    CHECK(f.state() == JournalActivationState::pending);
}

void stale_not_started_keeps_pending()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::not_started);
    const auto result = f.reconcile();
    CHECK(result.rejected_stale);
    CHECK(result.journal.state == JournalActivationState::pending);
    CHECK(f.state() == JournalActivationState::pending);
}

void stale_unavailable_keeps_pending()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::unavailable);
    const auto result = f.reconcile();
    CHECK(result.rejected_stale);
    CHECK(result.journal.state == JournalActivationState::pending);
}

void stale_not_started_blocks_new_identity()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::not_started);
    static_cast<void>(f.reconcile());
    CHECK_THROWS_AS(f.journal->begin_or_replay({1043}, generation, settings, f.backend),
                    std::logic_error);
    CHECK_EQ(f.backend.calls, 1);
}

void stale_started_survives_restart()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::started);
    static_cast<void>(f.reconcile());
    f.reopen();
    CHECK(f.state() == JournalActivationState::pending);
    CHECK_EQ(f.ledger->current().value, next_epoch.value);
}

void stale_not_started_survives_restart()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::not_started);
    static_cast<void>(f.reconcile());
    f.reopen();
    CHECK(f.state() == JournalActivationState::pending);
}

void stale_does_not_write_terminal()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::started);
    static_cast<void>(f.reconcile());
    CHECK_EQ(f.journal_hook.terminal_calls, 0);
}

void stale_can_be_repolled()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::not_started);
    static_cast<void>(f.reconcile());
    f.authority.use_reply_epoch = false;
    f.authority.status = AuthoritativeActivationStatus::started;
    const auto result = f.reconcile();
    CHECK(result.journal.state == JournalActivationState::started);
    CHECK_EQ(f.authority.calls, 2);
}

void stale_after_restart_can_reconcile()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::not_started);
    static_cast<void>(f.reconcile());
    f.reopen();
    f.authority.use_reply_epoch = false;
    CHECK(f.reconcile().journal.state == JournalActivationState::not_started);
}

void rotation_during_started_query_is_stale()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    f.authority.during_query = [&f]() { f.rotate(); };
    const auto result = f.reconcile();
    CHECK(result.rejected_stale);
    CHECK(result.journal.state == JournalActivationState::pending);
    CHECK_EQ(f.ledger->current().value, next_epoch.value);
}

void rotation_during_not_started_query_is_stale()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    f.authority.during_query = [&f]() { f.rotate(); };
    const auto result = f.reconcile();
    CHECK(result.rejected_stale);
    CHECK(result.journal.state == JournalActivationState::pending);
}

void post_query_rotation_can_reconcile_again()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    f.authority.during_query = [&f]() { f.rotate(); };
    static_cast<void>(f.reconcile());
    f.authority.during_query = {};
    CHECK(f.reconcile().journal.state == JournalActivationState::started);
}

void future_epoch_cannot_self_authorize()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    f.authority.use_reply_epoch = true;
    f.authority.reply_epoch = next_epoch;
    CHECK_THROWS_AS(f.reconcile(), std::invalid_argument);
    CHECK_EQ(f.ledger->current().value, first_epoch.value);
    CHECK(f.state() == JournalActivationState::pending);
}

void wrong_id_rejected()
{
    Fixture f;
    f.authority.wrong_id = true;
    f.authority.status = AuthoritativeActivationStatus::started;
    CHECK_THROWS_AS(f.reconcile(), std::invalid_argument);
    CHECK(f.state() == JournalActivationState::pending);
}

void wrong_generation_rejected()
{
    Fixture f;
    f.authority.wrong_generation = true;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    CHECK_THROWS_AS(f.reconcile(), std::invalid_argument);
    CHECK(f.state() == JournalActivationState::pending);
}

void terminal_replay_skips_query_after_rotation()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    static_cast<void>(f.reconcile());
    f.rotate();
    CHECK(f.reconcile().journal.state == JournalActivationState::started);
    CHECK_EQ(f.authority.calls, 1);
}

void terminal_replay_without_epoch()
{
    Fixture f{false};
    static_cast<void>(f.journal->confirm(attempt, ActivationResolution::confirmed_started));
    CHECK(f.reconcile().journal.state == JournalActivationState::started);
    CHECK_EQ(f.authority.calls, 0);
}

void missing_attempt_skips_query()
{
    Fixture f;
    CHECK_THROWS_AS(f.reconciler->reconcile({9999}, f.authority), std::invalid_argument);
    CHECK_EQ(f.authority.calls, 0);
}

void stale_negative_history_does_not_resolve()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::not_started);
    for (int i = 0; i < 4; ++i)
    {
        CHECK(f.reconcile().journal.state == JournalActivationState::pending);
    }
    CHECK_EQ(f.authority.calls, 4);
    CHECK_EQ(f.backend.calls, 1);
}

void valid_terminal_disk_failure_preserves_pending()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::started;
    f.journal_hook.throw_terminal = true;
    CHECK_THROWS_AS(f.reconcile(), std::runtime_error);
    CHECK(f.state() == JournalActivationState::pending);
    f.reopen();
    CHECK(f.state() == JournalActivationState::pending);
}

void valid_terminal_retry_after_disk_failure()
{
    Fixture f;
    f.authority.status = AuthoritativeActivationStatus::not_started;
    f.journal_hook.throw_terminal = true;
    CHECK_THROWS_AS(f.reconcile(), std::runtime_error);
    f.journal_hook.throw_terminal = false;
    CHECK(f.reconcile().journal.state == JournalActivationState::not_started);
}

void stale_ignores_terminal_write_hook()
{
    Fixture f;
    f.stale(AuthoritativeActivationStatus::not_started);
    f.journal_hook.throw_terminal = true;
    try
    {
        CHECK(f.reconcile().journal.state == JournalActivationState::pending);
    }
    catch (const std::runtime_error&)
    {
        // RED must fail by assertion, not an uncaught test-harness error.
        CHECK(false);
    }
    CHECK_EQ(f.journal_hook.terminal_calls, 0);
}

} // namespace

cpp_mastery::test::TestSuite make_runtime_fenced_reconciliation_suite()
{
    cpp_mastery::test::TestSuite suite{"fenced-reconciliation"};
    suite.add("empty ledger starts unfenced", ledger_starts_unfenced);
    suite.add("initial epoch survives restart", initial_epoch_persists);
    suite.add("epoch rotation survives restart", epoch_rotation_persists);
    suite.add("epoch monotonicity", epoch_monotonicity);
    suite.add("epoch may skip numeric values", epoch_can_skip_numbers);
    suite.add("epoch writer receives fence", epoch_write_hook_receives_fence);
    suite.add("epoch write failure retains previous", failed_epoch_write_preserves_old);
    suite.add("epoch write failure may retry", failed_epoch_write_can_retry);
    suite.add("second ledger writer refused", duplicate_writer_refused);
    suite.add("independent epoch ledgers", independent_epoch_files);
    suite.add("missing ledger directory refused", missing_directory_refused);
    suite.add("torn epoch record rejected", torn_epoch_record_rejected);
    suite.add("corrupted epoch record rejected", corrupted_epoch_record_rejected);
    suite.add("nonmonotonic epoch history rejected", decreasing_epoch_history_rejected);
    suite.add("no durable epoch blocks reconciliation", zero_epoch_blocks_reconciliation);
    suite.add("query uses current identity and epoch", query_is_fenced);
    suite.add("current owner confirms started", current_started_is_durable);
    suite.add("current owner confirms not started", current_not_started_is_durable);
    suite.add("current owner unavailable", current_unavailable_keeps_pending);
    suite.add("authority exception keeps pending", authority_exception_keeps_pending);
    suite.add("stale started evidence rejected", stale_started_keeps_pending);
    suite.add("stale no start evidence rejected", stale_not_started_keeps_pending);
    suite.add("stale unavailable evidence rejected", stale_unavailable_keeps_pending);
    suite.add("stale no start blocks next attempt", stale_not_started_blocks_new_identity);
    suite.add("stale started survives restart", stale_started_survives_restart);
    suite.add("stale no start survives restart", stale_not_started_survives_restart);
    suite.add("stale evidence never writes terminal", stale_does_not_write_terminal);
    suite.add("fresh owner resolves stale pending", stale_can_be_repolled);
    suite.add("fresh owner resolves after restart", stale_after_restart_can_reconcile);
    suite.add("ownership change during started query", rotation_during_started_query_is_stale);
    suite.add("ownership change during no start query", rotation_during_not_started_query_is_stale);
    suite.add("post query rotation allows new owner", post_query_rotation_can_reconcile_again);
    suite.add("future epoch cannot claim authority", future_epoch_cannot_self_authorize);
    suite.add("mismatched attempt ID rejected", wrong_id_rejected);
    suite.add("mismatched backend generation rejected", wrong_generation_rejected);
    suite.add("terminal replay ignores newer epoch", terminal_replay_skips_query_after_rotation);
    suite.add("terminal replay needs no epoch", terminal_replay_without_epoch);
    suite.add("missing attempt skips query", missing_attempt_skips_query);
    suite.add("repeated stale negative evidence", stale_negative_history_does_not_resolve);
    suite.add("terminal persistence failure keeps pending",
              valid_terminal_disk_failure_preserves_pending);
    suite.add("terminal persistence failure may retry", valid_terminal_retry_after_disk_failure);
    suite.add("stale evidence skips terminal write", stale_ignores_terminal_write_hook);
    return suite;
}
