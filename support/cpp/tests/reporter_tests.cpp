#include "bootstrap.hpp"

#include <cpp_mastery/testing/Reporter.hpp>

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace
{

namespace test = cpp_mastery::test;
using namespace std::chrono_literals;

[[nodiscard]] test::TestFailure
make_failure(std::string file, const std::uint_least32_t line,
             std::string expression = "value == 42",
             std::string message = "expected 42 = 42, actual value = 7")
{
    return test::TestFailure{.expression = std::move(expression),
                             .message = std::move(message),
                             .file = std::move(file),
                             .function = "experiment",
                             .line = line,
                             .column = 5};
}

void starts_run_and_suite_with_exact_headers(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    reporter.run_started("Voxel engine tests");
    reporter.suite_started("Chunk meshing");

    checks.expect(output.str() == "Voxel engine tests\n\nChunk meshing\n",
                  "run and suite headings have stable layout");
}

void successful_test_has_pass_label_only(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    test::TestResult result;
    result.name = "mesh is valid";
    reporter.test_finished(result);

    checks.expect(result.status() == test::TestStatus::passed, "empty result is passed");
    checks.expect(output.str() == "  [PASS] mesh is valid\n", "passed result has exact label");
}

void failed_test_renders_assertion_and_posix_basename(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    test::TestResult result;
    result.name = "invalid mesh";
    result.failures.push_back(make_failure("/home/adrian/project/tests/report.cpp", 81));
    reporter.test_finished(result);

    checks.expect(result.status() == test::TestStatus::failed, "recorded assertion is failure");
    checks.expect(output.str() == "  [FAIL] invalid mesh\n"
                                  "      report.cpp:81\n"
                                  "      value == 42\n"
                                  "      expected 42 = 42, actual value = 7\n",
                  "failed result includes location, expression and diagnostic");
}

void error_result_renders_unexpected_exception(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    test::TestResult result;
    result.name = "disk read";
    result.unexpected_exception = "disk unavailable";
    reporter.test_finished(result);

    checks.expect(result.status() == test::TestStatus::error, "unexpected exception is error");
    checks.expect(output.str() == "  [ERROR] disk read\n"
                                  "      unexpected exception: disk unavailable\n",
                  "error label and exception detail are preserved");
}

void error_takes_precedence_without_losing_assertion(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    test::TestResult result;
    result.suite = "diagnostics";
    result.name = "failure followed by exception";
    result.failures.push_back(make_failure("/home/adrian/project/tests/report.cpp", 81));
    result.unexpected_exception = "disk unavailable";
    reporter.test_finished(result);

    const std::string expected = "  [ERROR] failure followed by exception\n"
                                 "      report.cpp:81\n"
                                 "      value == 42\n"
                                 "      expected 42 = 42, actual value = 7\n"
                                 "      unexpected exception: disk unavailable\n";
    checks.expect(result.status() == test::TestStatus::error,
                  "error takes precedence over assertion failure");
    checks.expect(result.failures.size() == 1U, "failure is not discarded by error status");
    checks.expect(output.str() == expected, "both diagnostics appear in deterministic order");
    checks.expect(result.suite == "diagnostics", "reporter does not modify suite metadata");
}

void multiple_failures_keep_original_order(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    test::TestResult result;
    result.name = "two failures";
    result.failures.push_back(make_failure("first.cpp", 10, "a", "first issue"));
    result.failures.push_back(make_failure("second.cpp", 20, "b", "second issue"));
    reporter.test_finished(result);

    checks.expect(output.str() == "  [FAIL] two failures\n"
                                  "      first.cpp:10\n"
                                  "      a\n"
                                  "      first issue\n"
                                  "      second.cpp:20\n"
                                  "      b\n"
                                  "      second issue\n",
                  "multiple failures preserve their order and individual messages");
}

void basename_handles_windows_and_relative_paths(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    test::TestResult result;
    result.name = "paths";
    result.failures.push_back(
        make_failure(R"(C:\dev\tests\vector_tests.cpp)", 19, "win", "win diagnostic"));
    result.failures.push_back(make_failure("relative.cpp", 23, "relative", "relative diagnostic"));
    reporter.test_finished(result);

    const std::string rendered = output.str();
    checks.expect(rendered.find("      vector_tests.cpp:19\n") != std::string::npos,
                  "Windows path prints only basename");
    checks.expect(rendered.find("      relative.cpp:23\n") != std::string::npos,
                  "relative source filename is preserved");
    checks.expect(rendered.find("C:\\dev\\tests") == std::string::npos,
                  "full Windows path is not leaked to diagnostics");
}

void nonverbose_mode_excludes_all_durations(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    test::TestResult result;
    result.name = "timed";
    result.duration = 123456789ns;
    reporter.test_finished(result);
    const test::RunSummary summary{.total = 2, .passed = 2, .duration = 1987654321ns};
    reporter.run_finished(summary);

    checks.expect(output.str() == "  [PASS] timed\n"
                                  "\n2 tests: 2 passed, 0 failed, 0 errors\n",
                  "quiet output has no timing or extra tokens");
}

void verbose_test_duration_converts_to_microseconds(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{true, output};
    test::TestResult result;
    result.name = "timed";
    result.duration = 123456789ns;
    reporter.test_finished(result);

    checks.expect(output.str() == "  [PASS] timed (123456 us)\n",
                  "verbose test duration truncates nanoseconds to microseconds");
}

void verbose_summary_duration_converts_to_milliseconds(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{true, output};
    const test::RunSummary summary{.total = 2, .passed = 1, .failed = 1, .duration = 1987654321ns};
    reporter.run_finished(summary);

    checks.expect(output.str() == "\n2 tests: 1 passed, 1 failed, 0 errors (1987 ms)\n",
                  "verbose total duration truncates to milliseconds");
}

void one_test_uses_singular_grammar(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    const test::RunSummary summary{.total = 1, .passed = 1};
    reporter.run_finished(summary);

    checks.expect(output.str() == "\n1 test: 1 passed, 0 failed, 0 errors\n",
                  "one test uses singular noun in summary");
}

void zero_and_multiple_tests_use_plural_grammar(bootstrap::Checks& checks)
{
    std::ostringstream output;
    const test::ConsoleReporter reporter{false, output};
    reporter.run_finished(test::RunSummary{});
    reporter.run_finished(test::RunSummary{.total = 3, .passed = 1, .failed = 1, .errors = 1});

    checks.expect(output.str() == "\n0 tests: 0 passed, 0 failed, 0 errors\n"
                                  "\n3 tests: 1 passed, 1 failed, 1 error\n",
                  "zero and multiple tests use plural noun");
}

void reporters_do_not_share_output_state(bootstrap::Checks& checks)
{
    std::ostringstream first;
    std::ostringstream second;
    const test::ConsoleReporter a{false, first};
    const test::ConsoleReporter b{true, second};
    test::TestResult result;
    result.name = "isolation";
    result.duration = 1000000ns;
    a.test_finished(result);
    b.test_finished(result);

    checks.expect(first.str() == "  [PASS] isolation\n", "first reporter stays quiet");
    checks.expect(second.str() == "  [PASS] isolation (1000 us)\n",
                  "second reporter stays verbose");
}

} // namespace

void run_reporter_tests(bootstrap::Checks& checks)
{
    checks.run_case("Reporter: run and suite headings", starts_run_and_suite_with_exact_headers);
    checks.run_case("Reporter: passed label", successful_test_has_pass_label_only);
    checks.run_case("Reporter: failed label and POSIX source path",
                    failed_test_renders_assertion_and_posix_basename);
    checks.run_case("Reporter: unexpected exception", error_result_renders_unexpected_exception);
    checks.run_case("Reporter: error precedence and diagnostics",
                    error_takes_precedence_without_losing_assertion);
    checks.run_case("Reporter: multiple failures", multiple_failures_keep_original_order);
    checks.run_case("Reporter: Windows and relative source paths",
                    basename_handles_windows_and_relative_paths);
    checks.run_case("Reporter: quiet durations", nonverbose_mode_excludes_all_durations);
    checks.run_case("Reporter: verbose test duration",
                    verbose_test_duration_converts_to_microseconds);
    checks.run_case("Reporter: verbose run duration",
                    verbose_summary_duration_converts_to_milliseconds);
    checks.run_case("Reporter: singular summary", one_test_uses_singular_grammar);
    checks.run_case("Reporter: zero and plural summaries",
                    zero_and_multiple_tests_use_plural_grammar);
    checks.run_case("Reporter: stream isolation", reporters_do_not_share_output_state);
}
