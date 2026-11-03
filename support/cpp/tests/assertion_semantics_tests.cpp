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

void throws_runtime_error()
{
    throw std::runtime_error{"boom"};
}

void throws_out_of_range()
{
    throw std::out_of_range{"range"};
}

void throws_non_standard_exception()
{
    throw 42;
}

void returns_normally() {}

void check_equal_evaluates_once_and_reports_values(bootstrap::Checks& checks)
{
    test::TestResult result;
    int evaluations = 0;
    bool continued = false;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_EQ(++evaluations, 2);
        continued = true;
    }

    checks.expect(evaluations == 1, "CHECK_EQ evaluates its actual operand exactly once");
    checks.expect(continued, "failed CHECK_EQ is non-fatal");
    checks.expect(result.failures.size() == 1U, "failed CHECK_EQ records exactly one failure");
    checks.expect(result.status() == test::TestStatus::failed, "failed CHECK_EQ has failed status");
    if (result.failures.size() == 1U)
    {
        const auto& failure = result.failures.front();
        checks.expect(failure.expression == "++evaluations == 2",
                      "CHECK_EQ captures both source expressions");
        checks.expect(failure.message == "expected 2 = 2, actual ++evaluations = 1",
                      "CHECK_EQ reports both evaluated values");
    }
}

void passing_comparisons_record_nothing(bootstrap::Checks& checks)
{
    test::TestResult result;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_EQ(2, 2);
        CHECK_NE(1, 2);
    }

    checks.expect(result.failures.empty(), "passing comparisons record no failures");
    checks.expect(result.status() == test::TestStatus::passed,
                  "passing comparisons have passed status");
}

void check_not_equal_reports_matching_values(bootstrap::Checks& checks)
{
    test::TestResult result;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_NE(1, 1);
    }

    checks.expect(result.failures.size() == 1U, "matching operands fail CHECK_NE");
    if (result.failures.size() == 1U)
    {
        const auto& failure = result.failures.front();
        checks.expect(failure.expression == "1 != 1", "CHECK_NE records the expression");
        checks.expect(failure.message == "expected different values, both were 1",
                      "CHECK_NE reports the common value");
    }
}

struct Unprintable
{
    int value;

    [[nodiscard]] bool operator==(const Unprintable&) const = default;
};

void unprintable_values_have_safe_diagnostics(bootstrap::Checks& checks)
{
    test::TestResult result;
    const Unprintable actual{1};
    const Unprintable expected{2};
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_EQ(actual, expected);
    }

    checks.expect(result.failures.size() == 1U, "unprintable comparable values can fail safely");
    if (result.failures.size() == 1U)
    {
        checks.expect(result.failures.front().message ==
                          "expected expected = <unprintable>, actual actual = <unprintable>",
                      "non-streamable values receive fallback text");
    }
}

void matching_exception_assertions_pass(bootstrap::Checks& checks)
{
    test::TestResult result;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_THROWS_AS(throws_out_of_range(), std::out_of_range);
        CHECK_THROWS_AS(throws_out_of_range(), std::exception);
        CHECK_NO_THROW(returns_normally());
    }

    checks.expect(result.failures.empty(), "matching exception assertions record no failures");
    checks.expect(result.status() == test::TestStatus::passed,
                  "matching exception assertions have passed status");
}

void incorrect_exception_type_is_recorded(bootstrap::Checks& checks)
{
    test::TestResult result;
    bool continued = false;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_THROWS_AS(throws_runtime_error(), std::out_of_range);
        continued = true;
    }

    checks.expect(continued, "CHECK_THROWS_AS mismatch continues execution");
    checks.expect(result.failures.size() == 1U, "exception mismatch records one failure");
    checks.expect(!result.unexpected_exception.has_value(),
                  "caught assertion mismatch is not an unexpected runner exception");
    if (result.failures.size() == 1U)
    {
        const auto& failure = result.failures.front();
        checks.expect(failure.expression == "throws_runtime_error() throws std::out_of_range",
                      "exception mismatch identifies the expected type");
        checks.expect(failure.message == "threw a different std::exception: boom",
                      "exception mismatch preserves the diagnostic");
    }
}

void missing_exception_is_recorded(bootstrap::Checks& checks)
{
    test::TestResult result;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_THROWS_AS(returns_normally(), std::out_of_range);
    }

    checks.expect(result.failures.size() == 1U, "missing exception records a failure");
    if (result.failures.size() == 1U)
    {
        checks.expect(result.failures.front().message == "did not throw",
                      "missing exception has a useful diagnostic");
    }
}

void non_standard_exception_mismatch_is_recorded(bootstrap::Checks& checks)
{
    test::TestResult result;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_THROWS_AS(throws_non_standard_exception(), std::out_of_range);
    }

    checks.expect(result.failures.size() == 1U, "non-standard mismatch records a failure");
    if (result.failures.size() == 1U)
    {
        checks.expect(result.failures.front().message == "threw a different non-standard exception",
                      "non-standard mismatch has a useful diagnostic");
    }
}

void check_no_throw_records_standard_and_non_standard_exceptions(bootstrap::Checks& checks)
{
    test::TestResult result;
    bool continued = false;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_NO_THROW(throws_runtime_error());
        CHECK_NO_THROW(throws_non_standard_exception());
        continued = true;
    }

    checks.expect(continued, "CHECK_NO_THROW continues after caught exceptions");
    checks.expect(result.failures.size() == 2U, "CHECK_NO_THROW records both exceptions");
    checks.expect(!result.unexpected_exception.has_value(),
                  "caught exception assertion failures do not set unexpected_exception");
    if (result.failures.size() == 2U)
    {
        checks.expect(result.failures[0].message == "unexpected std::exception: boom",
                      "CHECK_NO_THROW captures std::exception::what");
        checks.expect(result.failures[1].message == "unexpected non-standard exception",
                      "CHECK_NO_THROW captures non-standard exception");
    }
}

void four_non_fatal_failures_accumulate(bootstrap::Checks& checks)
{
    test::TestResult result;
    int calls = 0;
    bool finished = false;
    {
        test::detail::ActiveResultGuard guard{result};
        CHECK_EQ(++calls, 2);
        CHECK_NE(calls, 1);
        CHECK_THROWS_AS(throws_runtime_error(), std::out_of_range);
        CHECK_NO_THROW(throws_runtime_error());
        finished = true;
    }

    checks.expect(calls == 1, "increment expression evaluates once");
    checks.expect(finished, "all non-fatal assertions execute");
    checks.expect(result.failures.size() == 4U, "four non-fatal checks record four failures");
    checks.expect(result.status() == test::TestStatus::failed,
                  "failed assertions yield failed status");
    checks.expect(!result.unexpected_exception.has_value(),
                  "caught exceptions do not become runner errors");
}

void fatal_equality_check_aborts_after_recording(bootstrap::Checks& checks)
{
    test::TestResult result;
    bool aborted = false;
    bool continued = false;
    {
        test::detail::ActiveResultGuard guard{result};
        try
        {
            REQUIRE_EQ(1, 2);
            continued = true;
        }
        catch (const test::AbortTest&)
        {
            aborted = true;
        }
    }

    checks.expect(aborted, "REQUIRE_EQ throws AbortTest");
    checks.expect(!continued, "REQUIRE_EQ stops normal execution");
    checks.expect(result.failures.size() == 1U, "REQUIRE_EQ records before aborting");
    checks.expect(result.status() == test::TestStatus::failed, "REQUIRE_EQ yields failed status");
}

void fatal_inequality_check_aborts_after_recording(bootstrap::Checks& checks)
{
    test::TestResult result;
    bool aborted = false;
    {
        test::detail::ActiveResultGuard guard{result};
        try
        {
            REQUIRE_NE(1, 1);
        }
        catch (const test::AbortTest&)
        {
            aborted = true;
        }
    }

    checks.expect(aborted, "REQUIRE_NE throws AbortTest");
    checks.expect(result.failures.size() == 1U, "REQUIRE_NE records before aborting");
}

void fatal_exception_assertions_abort_after_recording(bootstrap::Checks& checks)
{
    test::TestResult missing_result;
    test::TestResult unexpected_result;
    bool missing_aborted = false;
    bool unexpected_aborted = false;
    {
        test::detail::ActiveResultGuard guard{missing_result};
        try
        {
            REQUIRE_THROWS_AS(returns_normally(), std::runtime_error);
        }
        catch (const test::AbortTest&)
        {
            missing_aborted = true;
        }
    }
    {
        test::detail::ActiveResultGuard guard{unexpected_result};
        try
        {
            REQUIRE_NO_THROW(throws_runtime_error());
        }
        catch (const test::AbortTest&)
        {
            unexpected_aborted = true;
        }
    }

    checks.expect(missing_aborted, "REQUIRE_THROWS_AS throws AbortTest on mismatch");
    checks.expect(unexpected_aborted, "REQUIRE_NO_THROW throws AbortTest on mismatch");
    checks.expect(missing_result.failures.size() == 1U,
                  "REQUIRE_THROWS_AS records before aborting");
    checks.expect(unexpected_result.failures.size() == 1U,
                  "REQUIRE_NO_THROW records before aborting");
}

void equality_macro_preserves_call_site_location(bootstrap::Checks& checks)
{
    test::TestResult result;
    std::uint_least32_t invocation_line = 0;
    {
        test::detail::ActiveResultGuard guard{result};
        invocation_line = std::source_location::current().line() + 1;
        CHECK_EQ(1, 2);
    }

    checks.expect(result.failures.size() == 1U, "equality mismatch records a failure");
    if (result.failures.size() == 1U)
    {
        checks.expect(result.failures.front().line == invocation_line,
                      "equality macro preserves invocation line");
        checks.expect(result.failures.front().file == __FILE__,
                      "equality macro preserves invocation file");
    }
}

} // namespace

void run_assertion_semantics_tests(bootstrap::Checks& checks)
{
    checks.run_case("CHECK_EQ evaluates once", check_equal_evaluates_once_and_reports_values);
    checks.run_case("successful comparisons", passing_comparisons_record_nothing);
    checks.run_case("CHECK_NE diagnostics", check_not_equal_reports_matching_values);
    checks.run_case("unprintable comparison values", unprintable_values_have_safe_diagnostics);
    checks.run_case("matching exceptions", matching_exception_assertions_pass);
    checks.run_case("incorrect exception type", incorrect_exception_type_is_recorded);
    checks.run_case("missing exception", missing_exception_is_recorded);
    checks.run_case("non-standard exception mismatch", non_standard_exception_mismatch_is_recorded);
    checks.run_case("CHECK_NO_THROW mismatches",
                    check_no_throw_records_standard_and_non_standard_exceptions);
    checks.run_case("four non-fatal failures", four_non_fatal_failures_accumulate);
    checks.run_case("fatal equality check", fatal_equality_check_aborts_after_recording);
    checks.run_case("fatal inequality check", fatal_inequality_check_aborts_after_recording);
    checks.run_case("fatal exception checks", fatal_exception_assertions_abort_after_recording);
    checks.run_case("equality call-site location", equality_macro_preserves_call_site_location);
}
