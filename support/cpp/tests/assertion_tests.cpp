#include "bootstrap.hpp"

#include <cpp_mastery/testing/Assertions.hpp>
#include <cpp_mastery/testing/TestContext.hpp>
#include <cpp_mastery/testing/TestResult.hpp>

#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string_view>

namespace
{

namespace test = cpp_mastery::test;

void successful_check_records_nothing(bootstrap::Checks& checks)
{
    test::TestResult result;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK(true);
    }

    checks.expect(result.failures.empty(), "successful CHECK records no failures");
    checks.expect(result.status() == test::TestStatus::passed, "successful CHECK passes");
}

void failed_check_records_failure_and_continues(bootstrap::Checks& checks)
{
    test::TestResult result;
    bool continued = false;
    std::uint_least32_t expected_line = 0;
    {
        test::detail::ActiveResultGuard guard{result};
        expected_line = std::source_location::current().line() + 1;
        CHECK(false);
        continued = true;
    }

    checks.expect(continued, "failed CHECK continues execution");
    checks.expect(result.failures.size() == 1U, "failed CHECK records one failure");
    checks.expect(result.status() == test::TestStatus::failed, "failed CHECK has failed status");
    if (result.failures.size() == 1U)
    {
        const auto& failure = result.failures.front();
        checks.expect(failure.expression == "false", "CHECK captures the source expression");
        checks.expect(failure.message == "condition evaluated to false",
                      "CHECK records the message");
        checks.expect(failure.file == __FILE__, "CHECK captures the correct source file");
        checks.expect(failure.line == expected_line, "CHECK captures the invocation line");
    }
}

void failed_require_records_failure_and_aborts(bootstrap::Checks& checks)
{
    test::TestResult result;
    bool caught_abort = false;
    bool continued = false;
    {
        test::detail::ActiveResultGuard guard{result};
        try
        {
            REQUIRE(false);
            continued = true;
        }
        catch (const test::AbortTest&)
        {
            caught_abort = true;
        }
    }

    checks.expect(caught_abort, "failed REQUIRE throws AbortTest");
    checks.expect(!continued, "failed REQUIRE stops the current execution path");
    checks.expect(result.failures.size() == 1U, "REQUIRE records before aborting");
    checks.expect(result.status() == test::TestStatus::failed, "failed REQUIRE is not an error");
}

void nonfatal_failures_accumulate(bootstrap::Checks& checks)
{
    test::TestResult result;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK(false);
        CHECK_EQ(2, 5);
    }

    checks.expect(result.failures.size() == 2U, "multiple CHECK failures accumulate");
    if (result.failures.size() == 2U)
    {
        checks.expect(result.failures[0].expression == "false",
                      "first failure keeps its expression");
        checks.expect(result.failures[1].expression == "2 == 5", "CHECK_EQ keeps both expressions");
        checks.expect(result.failures[1].message == "expected 5 = 5, actual 2 = 2",
                      "CHECK_EQ includes both values");
    }
}

void source_location_is_preserved(bootstrap::Checks& checks)
{
    test::TestResult result;
    const auto location = std::source_location::current();
    {
        test::detail::ActiveResultGuard guard{result};
        test::detail::check(false, "health > 0", test::detail::FailureMode::continue_test,
                            location);
    }

    checks.expect(result.failures.size() == 1U, "source-location check fails once");
    if (result.failures.size() == 1U)
    {
        const auto& failure = result.failures.front();
        checks.expect(failure.file == location.file_name(), "failure preserves the file");
        checks.expect(failure.function == location.function_name(),
                      "failure preserves the function");
        checks.expect(failure.line == location.line(), "failure preserves the line");
        checks.expect(failure.column == location.column(), "failure preserves the column");
    }
}

void nested_context_restores_previous_result(bootstrap::Checks& checks)
{
    test::TestResult outer;
    test::TestResult inner;
    test::TestResult* const initial = test::detail::active_result;

    {
        test::detail::ActiveResultGuard outer_guard{outer};
        checks.expect(test::detail::active_result == &outer, "outer context becomes active");
        {
            test::detail::ActiveResultGuard inner_guard{inner};
            checks.expect(test::detail::active_result == &inner, "inner context becomes active");
            CHECK(false);
        }
        checks.expect(test::detail::active_result == &outer, "outer context is restored");
        CHECK(false);
    }

    checks.expect(test::detail::active_result == initial, "original context is restored");
    checks.expect(outer.failures.size() == 1U, "outer failure belongs to outer result");
    checks.expect(inner.failures.size() == 1U, "inner failure belongs to inner result");
}

void context_restores_after_exception(bootstrap::Checks& checks)
{
    test::TestResult result;
    test::TestResult* const initial = test::detail::active_result;
    bool caught = false;
    try
    {
        test::detail::ActiveResultGuard guard{result};
        throw std::runtime_error{"simulated failure"};
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }

    checks.expect(caught, "the simulated exception was caught");
    checks.expect(test::detail::active_result == initial,
                  "exception restores the previous context");
}

} // namespace

void run_assertion_tests(bootstrap::Checks& checks)
{
    checks.run_case("successful CHECK", successful_check_records_nothing);
    checks.run_case("failed CHECK", failed_check_records_failure_and_continues);
    checks.run_case("failed REQUIRE", failed_require_records_failure_and_aborts);
    checks.run_case("multiple CHECK failures", nonfatal_failures_accumulate);
    checks.run_case("source location", source_location_is_preserved);
    checks.run_case("nested test contexts", nested_context_restores_previous_result);
    checks.run_case("exception safety of test context", context_restores_after_exception);
}
