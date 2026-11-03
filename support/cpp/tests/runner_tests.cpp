#include "bootstrap.hpp"

#include <cpp_mastery/testing/Runner.hpp>
#include <cpp_mastery/testing/TestContext.hpp>

#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

namespace test = cpp_mastery::test;

int pass_calls = 0;
int fail_calls = 0;
int fatal_calls = 0;
int reached_after_fatal = 0;

void reset_calls()
{
    pass_calls = 0;
    fail_calls = 0;
    fatal_calls = 0;
    reached_after_fatal = 0;
}

void passing_test()
{
    ++pass_calls;
    CHECK(true);
}

void failing_test()
{
    ++fail_calls;
    CHECK(false);
}

void fatal_test()
{
    ++fatal_calls;
    REQUIRE(false);
    ++reached_after_fatal;
}

void throws_standard_exception()
{
    throw std::runtime_error{"runner boom"};
}

void throws_nonstandard_exception()
{
    throw 42;
}

[[nodiscard]] test::Runner make_basic_runner()
{
    test::Runner runner{"Runner self-tests"};
    test::TestSuite suite{"Basic"};
    suite.add("passes", passing_test);
    suite.add("fails", failing_test);
    runner.add(std::move(suite));
    return runner;
}

[[nodiscard]] test::Runner make_single_runner(const std::string_view name,
                                              const test::TestFunction function)
{
    test::Runner runner{"Runner self-tests"};
    test::TestSuite suite{"Basic"};
    suite.add(std::string{name}, function);
    runner.add(std::move(suite));
    return runner;
}

struct CapturedRun
{
    int exit_code;
    std::string output;
    std::string errors;
};

[[nodiscard]] CapturedRun
run_with_arguments(const test::Runner& runner,
                   const std::initializer_list<std::string_view> arguments = {})
{
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1U);
    storage.emplace_back("runner");
    for (const std::string_view argument : arguments)
    {
        storage.emplace_back(argument);
    }

    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (std::string& argument : storage)
    {
        argv.push_back(argument.data());
    }

    std::ostringstream output;
    std::ostringstream errors;
    const int exit_code = runner.run(static_cast<int>(argv.size()), argv.data(), output, errors);
    return CapturedRun{exit_code, output.str(), errors.str()};
}

[[nodiscard]] bool contains(const std::string& haystack, const std::string_view needle)
{
    return haystack.find(needle) != std::string::npos;
}

void unfiltered_run_reports_failure(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner());

    checks.expect(run.exit_code == 1, "unfiltered runner returns 1 when one case fails");
    checks.expect(pass_calls == 1 && fail_calls == 1, "unfiltered runner executes both cases");
    checks.expect(contains(run.output, "[PASS] passes"), "runner reports passing case");
    checks.expect(contains(run.output, "[FAIL] fails"), "runner reports failing case");
    checks.expect(contains(run.output, "2 tests: 1 passed, 1 failed, 0 errors"),
                  "runner summarizes pass/failure totals");
    checks.expect(run.errors.empty(), "normal test failures are not CLI errors");
}

void passing_filter_returns_zero(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--test", "passes"});

    checks.expect(run.exit_code == 0, "passing filtered test exits with 0");
    checks.expect(pass_calls == 1 && fail_calls == 0, "test filter only executes passes");
    checks.expect(contains(run.output, "1 tests: 1 passed, 0 failed, 0 errors"),
                  "filtered summary counts only executed cases");
}

void missing_test_filter_returns_two(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--test", "nonexistent"});

    checks.expect(run.exit_code == 2, "unmatched test selection exits with 2");
    checks.expect(pass_calls == 0 && fail_calls == 0, "unmatched filter executes no tests");
    checks.expect(contains(run.errors, "No tests matched"),
                  "unmatched filter explains the selection failure");
}

void list_mode_does_not_execute_tests(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--list"});

    checks.expect(run.exit_code == 0, "--list exits with 0");
    checks.expect(pass_calls == 0 && fail_calls == 0, "--list does not execute cases");
    checks.expect(contains(run.output, "Basic\n  passes\n  fails\n"),
                  "--list prints registered suite and test names");
    checks.expect(!contains(run.output, "[FAIL]"), "--list does not report test outcomes");
}

void unknown_option_returns_two(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--unknown"});

    checks.expect(run.exit_code == 2, "unknown option exits with 2");
    checks.expect(pass_calls == 0 && fail_calls == 0, "invalid option executes no tests");
    checks.expect(contains(run.errors, "unknown test-runner option: --unknown"),
                  "invalid option is diagnosed on error stream");
    checks.expect(contains(run.output, "Usage: runner"), "invalid option prints usage");
}

void exact_case_insensitive_suite_filter(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--suite", "bAsIc"});
    checks.expect(run.exit_code == 1, "case-insensitive suite name selects Basic");
    checks.expect(pass_calls == 1 && fail_calls == 1, "matching suite runs both cases");

    reset_calls();
    const auto partial = run_with_arguments(make_basic_runner(), {"--suite", "bas"});
    checks.expect(partial.exit_code == 2, "suite filter requires an exact name");
    checks.expect(pass_calls == 0 && fail_calls == 0, "partial suite name matches nothing");
}

void case_insensitive_substring_test_filter(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--test", "AsSe"});

    checks.expect(run.exit_code == 0, "test name filter is case-insensitive");
    checks.expect(pass_calls == 1 && fail_calls == 0, "test name filter supports substrings");
}

void suite_and_test_filters_combine(bootstrap::Checks& checks)
{
    reset_calls();
    test::Runner runner{"Runner self-tests"};
    test::TestSuite basic{"Basic"};
    basic.add("passes", passing_test);
    test::TestSuite extra{"Extra"};
    extra.add("fails", failing_test);
    runner.add(std::move(basic)).add(std::move(extra));

    const auto run = run_with_arguments(runner, {"--suite", "BASIC", "--test", "PAS"});
    checks.expect(run.exit_code == 0, "combined filters select one passing test");
    checks.expect(pass_calls == 1 && fail_calls == 0, "combined filters exclude other suite");
    checks.expect(!contains(run.output, "Extra\n"), "nonmatching suite is not reported");
}

void missing_option_values_return_two(bootstrap::Checks& checks)
{
    reset_calls();
    const auto suite = run_with_arguments(make_basic_runner(), {"--suite"});
    const auto test_name = run_with_arguments(make_basic_runner(), {"--test"});
    checks.expect(suite.exit_code == 2 && test_name.exit_code == 2,
                  "missing option values return 2");
    checks.expect(contains(suite.errors, "--suite requires a value"),
                  "--suite missing value is explained");
    checks.expect(contains(test_name.errors, "--test requires a value"),
                  "--test missing value is explained");
    checks.expect(pass_calls == 0 && fail_calls == 0, "invalid options run no tests");
}

void help_returns_zero_without_running(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--help"});
    checks.expect(run.exit_code == 0, "--help returns zero");
    checks.expect(contains(run.output, "Usage: runner"), "--help prints usage");
    checks.expect(pass_calls == 0 && fail_calls == 0, "--help executes no tests");
}

void listing_respects_filters(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run =
        run_with_arguments(make_basic_runner(), {"--list", "--suite", "basic", "--test", "PAS"});
    checks.expect(run.exit_code == 0, "filtered --list succeeds");
    checks.expect(contains(run.output, "  passes\n"), "matching case is listed");
    checks.expect(!contains(run.output, "  fails\n"), "nonmatching case is not listed");
    checks.expect(pass_calls == 0 && fail_calls == 0, "filtered list executes nothing");
}

void empty_listing_still_succeeds(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--list", "--test", "nope"});
    checks.expect(run.exit_code == 0, "--list with no matches returns zero by current contract");
    checks.expect(contains(run.output, "No tests matched"), "empty listing is explained");
    checks.expect(run.errors.empty(), "empty listing is not a CLI error");
}

void verbose_mode_includes_timing(bootstrap::Checks& checks)
{
    reset_calls();
    const auto run = run_with_arguments(make_basic_runner(), {"--verbose", "--test", "passes"});
    checks.expect(run.exit_code == 0, "--verbose does not change success code");
    checks.expect(contains(run.output, " us)"), "verbose test record includes microseconds");
    checks.expect(contains(run.output, " ms)"), "verbose summary includes milliseconds");
}

void standard_exception_is_reported_as_error(bootstrap::Checks& checks)
{
    const auto run = run_with_arguments(make_single_runner("throws", throws_standard_exception));
    checks.expect(run.exit_code == 1, "uncaught standard exception returns test-failure code");
    checks.expect(contains(run.output, "[ERROR] throws"), "standard exception is an error");
    checks.expect(contains(run.output, "unexpected exception: runner boom"),
                  "runner preserves exception diagnostic");
    checks.expect(contains(run.output, "1 tests: 0 passed, 0 failed, 1 errors"),
                  "runner tracks unexpected errors separately");
}

void nonstandard_exception_is_reported_as_error(bootstrap::Checks& checks)
{
    const auto run = run_with_arguments(make_single_runner("throws", throws_nonstandard_exception));
    checks.expect(run.exit_code == 1, "uncaught non-standard exception returns 1");
    checks.expect(contains(run.output, "[ERROR] throws"), "non-standard exception is an error");
    checks.expect(contains(run.output, "unknown non-standard exception"),
                  "non-standard exception is diagnosed");
}

void null_test_function_is_reported_as_error(bootstrap::Checks& checks)
{
    const auto run = run_with_arguments(make_single_runner("null function", nullptr));
    checks.expect(run.exit_code == 1, "null test function returns test-failure code");
    checks.expect(contains(run.output, "[ERROR] null function"),
                  "null function is classified as error");
    checks.expect(contains(run.output, "test case has no function"),
                  "null function has actionable diagnostic");
}

void fatal_require_aborts_only_current_case(bootstrap::Checks& checks)
{
    reset_calls();
    test::Runner runner{"Runner self-tests"};
    test::TestSuite suite{"Basic"};
    suite.add("fatal", fatal_test);
    suite.add("passes", passing_test);
    runner.add(std::move(suite));
    const auto run = run_with_arguments(runner);

    checks.expect(run.exit_code == 1, "failed REQUIRE returns failure exit code");
    checks.expect(fatal_calls == 1 && reached_after_fatal == 0,
                  "REQUIRE stops execution of the failing case");
    checks.expect(pass_calls == 1, "subsequent case still executes");
    checks.expect(contains(run.output, "2 tests: 1 passed, 1 failed, 0 errors"),
                  "fatal REQUIRE is failure, not unexpected error");
}

void runner_restores_preexisting_test_context(bootstrap::Checks& checks)
{
    reset_calls();
    test::TestResult outer;
    const auto* const before = test::detail::active_result;
    {
        test::detail::ActiveResultGuard guard{outer};
        const auto run = run_with_arguments(make_basic_runner(), {"--test", "fails"});
        checks.expect(run.exit_code == 1, "inner runner records failing case");
        checks.expect(test::detail::active_result == &outer,
                      "runner restores the previous active assertion result");
        checks.expect(outer.failures.empty(), "inner failures do not pollute outer result");
    }
    checks.expect(test::detail::active_result == before,
                  "outer active context also restores its predecessor");
}

} // namespace

void run_runner_tests(bootstrap::Checks& checks)
{
    checks.run_case("Runner: unfiltered failure", unfiltered_run_reports_failure);
    checks.run_case("Runner: passing filter", passing_filter_returns_zero);
    checks.run_case("Runner: unmatched test filter", missing_test_filter_returns_two);
    checks.run_case("Runner: list", list_mode_does_not_execute_tests);
    checks.run_case("Runner: unknown option", unknown_option_returns_two);
    checks.run_case("Runner: suite filter", exact_case_insensitive_suite_filter);
    checks.run_case("Runner: test filter", case_insensitive_substring_test_filter);
    checks.run_case("Runner: combined filters", suite_and_test_filters_combine);
    checks.run_case("Runner: missing values", missing_option_values_return_two);
    checks.run_case("Runner: help", help_returns_zero_without_running);
    checks.run_case("Runner: filtered list", listing_respects_filters);
    checks.run_case("Runner: empty list", empty_listing_still_succeeds);
    checks.run_case("Runner: verbose", verbose_mode_includes_timing);
    checks.run_case("Runner: standard exception", standard_exception_is_reported_as_error);
    checks.run_case("Runner: nonstandard exception", nonstandard_exception_is_reported_as_error);
    checks.run_case("Runner: null function", null_test_function_is_reported_as_error);
    checks.run_case("Runner: fatal REQUIRE", fatal_require_aborts_only_current_case);
    checks.run_case("Runner: nested active context", runner_restores_preexisting_test_context);
}
