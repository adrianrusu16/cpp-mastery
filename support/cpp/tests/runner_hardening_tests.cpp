#include "bootstrap.hpp"

#include <cpp_mastery/testing/Runner.hpp>
#include <cpp_mastery/testing/TestContext.hpp>

#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace
{

namespace test = cpp_mastery::test;

int succeeding_calls = 0;

void bare_abort()
{
    throw test::AbortTest{};
}

void check_then_bare_abort()
{
    CHECK(false);
    throw test::AbortTest{};
}

void expected_fatal_abort()
{
    REQUIRE(false);
}

void check_then_expected_fatal_abort()
{
    CHECK(false);
    REQUIRE(false);
}

void succeeds()
{
    ++succeeding_calls;
    CHECK(true);
}

struct CapturedRun
{
    int exit_code;
    std::string output;
    std::string errors;
};

[[nodiscard]] CapturedRun run_test(const std::string_view name, const test::TestFunction function,
                                   const bool include_following_pass = false)
{
    test::TestSuite suite{"Hardening"};
    suite.add(std::string{name}, function);
    if (include_following_pass)
    {
        suite.add("following pass", succeeds);
    }

    test::Runner runner{"Runner hardening"};
    runner.add(std::move(suite));
    char executable[] = "runner";
    char* arguments[] = {executable};
    std::ostringstream output;
    std::ostringstream errors;
    const int exit_code = runner.run(1, arguments, output, errors);
    return CapturedRun{exit_code, output.str(), errors.str()};
}

[[nodiscard]] bool contains(const std::string& text, const std::string_view needle)
{
    return text.find(needle) != std::string::npos;
}

void bare_abort_is_an_execution_error(bootstrap::Checks& checks)
{
    const auto run = run_test("bare abort", bare_abort);
    checks.expect(run.exit_code == 1, "bare AbortTest must fail the runner");
    checks.expect(contains(run.output, "[ERROR] bare abort"),
                  "bare AbortTest is classified as ERROR, not PASS");
    checks.expect(contains(run.output, "unexpected exception: AbortTest escaped"),
                  "bare AbortTest is diagnosed");
    checks.expect(contains(run.output, "1 test: 0 passed, 0 failed, 1 error"),
                  "bare AbortTest contributes to the error count");
    checks.expect(run.errors.empty(), "unexpected test abort does not signal a CLI error");
}

void earlier_check_does_not_legitimize_bare_abort(bootstrap::Checks& checks)
{
    const auto run = run_test("failed check then abort", check_then_bare_abort);
    checks.expect(run.exit_code == 1, "CHECK plus bare abort fails the runner");
    checks.expect(contains(run.output, "[ERROR] failed check then abort"),
                  "a prior CHECK cannot hide an unexpected abort");
    checks.expect(contains(run.output, "condition evaluated to false"),
                  "previous assertion diagnostics remain visible");
    checks.expect(contains(run.output, "unexpected exception: AbortTest escaped"),
                  "unexpected abort is reported alongside prior failure");
    checks.expect(contains(run.output, "1 test: 0 passed, 0 failed, 1 error"),
                  "unexpected abort takes precedence over recorded failures");
}

void real_require_remains_a_regular_failure(bootstrap::Checks& checks)
{
    const auto run = run_test("requires", expected_fatal_abort);
    checks.expect(run.exit_code == 1, "REQUIRE failure returns test failure code");
    checks.expect(contains(run.output, "[FAIL] requires"), "real REQUIRE remains a failure");
    checks.expect(!contains(run.output, "[ERROR]"), "real REQUIRE is not an error");
    checks.expect(!contains(run.output, "unexpected exception:"),
                  "real REQUIRE has no unexpected exception");
    checks.expect(contains(run.output, "1 test: 0 passed, 1 failed, 0 errors"),
                  "real REQUIRE increments failed rather than errors");
}

void prior_check_and_require_remain_regular_failures(bootstrap::Checks& checks)
{
    const auto run = run_test("check then require", check_then_expected_fatal_abort);
    checks.expect(run.exit_code == 1, "CHECK then REQUIRE fails the runner");
    checks.expect(contains(run.output, "[FAIL] check then require"),
                  "CHECK then REQUIRE remains classified as failure");
    checks.expect(!contains(run.output, "unexpected exception:"),
                  "framework-generated AbortTest is not unexpected");
    checks.expect(contains(run.output, "1 test: 0 passed, 1 failed, 0 errors"),
                  "multiple assertion failures count as one failed test");
}

void abnormal_abort_does_not_stop_following_cases(bootstrap::Checks& checks)
{
    succeeding_calls = 0;
    const auto run = run_test("bare abort", bare_abort, true);
    checks.expect(run.exit_code == 1, "unexpected abort makes run fail");
    checks.expect(succeeding_calls == 1, "subsequent case executes normally");
    checks.expect(contains(run.output, "[PASS] following pass"),
                  "subsequent case is reported as passing");
    checks.expect(contains(run.output, "2 tests: 1 passed, 0 failed, 1 error"),
                  "summary reports one error and one pass");
}

void unexpected_abort_restores_outer_context(bootstrap::Checks& checks)
{
    test::TestResult outer;
    const auto* const initial = test::detail::active_result;
    {
        test::detail::ActiveResultGuard guard{outer};
        const auto run = run_test("bare abort", bare_abort);
        checks.expect(run.exit_code == 1, "nested bare abort fails");
        checks.expect(test::detail::active_result == &outer,
                      "nested runner restores previous active result");
        checks.expect(outer.failures.empty(), "nested bare abort does not pollute outer result");
    }
    checks.expect(test::detail::active_result == initial,
                  "outer guard restores initial test context");
}

} // namespace

void run_runner_hardening_tests(bootstrap::Checks& checks)
{
    checks.run_case("Hardening: bare AbortTest", bare_abort_is_an_execution_error);
    checks.run_case("Hardening: CHECK then bare AbortTest",
                    earlier_check_does_not_legitimize_bare_abort);
    checks.run_case("Hardening: legitimate REQUIRE", real_require_remains_a_regular_failure);
    checks.run_case("Hardening: CHECK then REQUIRE",
                    prior_check_and_require_remain_regular_failures);
    checks.run_case("Hardening: continue after bare abort",
                    abnormal_abort_does_not_stop_following_cases);
    checks.run_case("Hardening: restore active context", unexpected_abort_restores_outer_context);
}
