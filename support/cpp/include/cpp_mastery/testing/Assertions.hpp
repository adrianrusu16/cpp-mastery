#pragma once

#include "TestFailure.hpp"
#include "TestResult.hpp"

#include <concepts>
#include <exception>
#include <ostream>
#include <sstream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cpp_mastery::test {
namespace detail {

inline thread_local TestResult* active_result = nullptr;

enum class FailureMode {
    continue_test,
    abort_test,
};

template<typename T>
concept StreamInsertable = requires(std::ostream& output, const T& value) {
    output << value;
};

template<typename T>
[[nodiscard]] std::string value_to_string(const T& value)
{
    if constexpr (StreamInsertable<T>) {
        std::ostringstream output;
        output << value;
        return output.str();
    } else {
        return "<unprintable>";
    }
}

inline void record_failure(
    std::string expression,
    std::string message,
    const std::source_location location)
{
    if (active_result == nullptr) {
        throw std::logic_error{"test assertion executed without an active test"};
    }

    active_result->failures.push_back(TestFailure{
        .expression = std::move(expression),
        .message = std::move(message),
        .file = location.file_name(),
        .function = location.function_name(),
        .line = location.line(),
        .column = location.column(),
    });
}

inline void check(
    const bool condition,
    const std::string_view expression,
    const FailureMode mode,
    const std::source_location location)
{
    if (condition) {
        return;
    }

    record_failure(std::string{expression}, "condition evaluated to false", location);
    if (mode == FailureMode::abort_test) {
        throw AbortTest{};
    }
}

template<typename Actual, typename Expected>
void check_equal(
    const Actual& actual,
    const Expected& expected,
    const std::string_view actual_expression,
    const std::string_view expected_expression,
    const FailureMode mode,
    const std::source_location location)
{
    if (actual == expected) {
        return;
    }

    std::ostringstream message;
    message << "expected " << expected_expression << " = " << value_to_string(expected)
            << ", actual " << actual_expression << " = " << value_to_string(actual);

    record_failure(
        std::string{actual_expression} + " == " + std::string{expected_expression},
        message.str(),
        location);

    if (mode == FailureMode::abort_test) {
        throw AbortTest{};
    }
}

template<typename Actual, typename Expected>
void check_not_equal(
    const Actual& actual,
    const Expected& expected,
    const std::string_view actual_expression,
    const std::string_view expected_expression,
    const FailureMode mode,
    const std::source_location location)
{
    if (!(actual == expected)) {
        return;
    }

    std::ostringstream message;
    message << "expected different values, both were " << value_to_string(actual);

    record_failure(
        std::string{actual_expression} + " != " + std::string{expected_expression},
        message.str(),
        location);

    if (mode == FailureMode::abort_test) {
        throw AbortTest{};
    }
}

template<typename Exception, typename Callable>
void check_throws_as(
    Callable&& callable,
    const std::string_view expression,
    const std::string_view exception_name,
    const FailureMode mode,
    const std::source_location location)
{
    bool matched = false;
    std::string mismatch;

    try {
        std::forward<Callable>(callable)();
    } catch (const Exception&) {
        matched = true;
    } catch (const std::exception& error) {
        mismatch = std::string{"threw a different std::exception: "} + error.what();
    } catch (...) {
        mismatch = "threw a different non-standard exception";
    }

    if (matched) {
        return;
    }

    if (mismatch.empty()) {
        mismatch = "did not throw";
    }

    record_failure(
        std::string{expression} + " throws " + std::string{exception_name},
        std::move(mismatch),
        location);

    if (mode == FailureMode::abort_test) {
        throw AbortTest{};
    }
}

template<typename Callable>
void check_no_throw(
    Callable&& callable,
    const std::string_view expression,
    const FailureMode mode,
    const std::source_location location)
{
    try {
        std::forward<Callable>(callable)();
        return;
    } catch (const std::exception& error) {
        record_failure(
            std::string{expression},
            std::string{"unexpected std::exception: "} + error.what(),
            location);
    } catch (...) {
        record_failure(std::string{expression}, "unexpected non-standard exception", location);
    }

    if (mode == FailureMode::abort_test) {
        throw AbortTest{};
    }
}

} // namespace detail
} // namespace cpp_mastery::test

#define CHECK(expression) \
    ::cpp_mastery::test::detail::check( \
        static_cast<bool>(expression), #expression, ::cpp_mastery::test::detail::FailureMode::continue_test, std::source_location::current())

#define REQUIRE(expression) \
    ::cpp_mastery::test::detail::check( \
        static_cast<bool>(expression), #expression, ::cpp_mastery::test::detail::FailureMode::abort_test, std::source_location::current())

#define CHECK_FALSE(expression) CHECK(!(expression))
#define REQUIRE_FALSE(expression) REQUIRE(!(expression))

#define CHECK_EQ(actual, expected) \
    ::cpp_mastery::test::detail::check_equal( \
        (actual), (expected), #actual, #expected, ::cpp_mastery::test::detail::FailureMode::continue_test, std::source_location::current())

#define REQUIRE_EQ(actual, expected) \
    ::cpp_mastery::test::detail::check_equal( \
        (actual), (expected), #actual, #expected, ::cpp_mastery::test::detail::FailureMode::abort_test, std::source_location::current())

#define CHECK_NE(actual, expected) \
    ::cpp_mastery::test::detail::check_not_equal( \
        (actual), (expected), #actual, #expected, ::cpp_mastery::test::detail::FailureMode::continue_test, std::source_location::current())

#define REQUIRE_NE(actual, expected) \
    ::cpp_mastery::test::detail::check_not_equal( \
        (actual), (expected), #actual, #expected, ::cpp_mastery::test::detail::FailureMode::abort_test, std::source_location::current())

#define CHECK_THROWS_AS(expression, exception_type) \
    ::cpp_mastery::test::detail::check_throws_as<exception_type>( \
        [&]() { static_cast<void>(expression); }, #expression, #exception_type, ::cpp_mastery::test::detail::FailureMode::continue_test, \
        std::source_location::current())

#define REQUIRE_THROWS_AS(expression, exception_type) \
    ::cpp_mastery::test::detail::check_throws_as<exception_type>( \
        [&]() { static_cast<void>(expression); }, #expression, #exception_type, ::cpp_mastery::test::detail::FailureMode::abort_test, \
        std::source_location::current())

#define CHECK_NO_THROW(expression) \
    ::cpp_mastery::test::detail::check_no_throw( \
        [&]() { static_cast<void>(expression); }, #expression, ::cpp_mastery::test::detail::FailureMode::continue_test, std::source_location::current())

#define REQUIRE_NO_THROW(expression) \
    ::cpp_mastery::test::detail::check_no_throw( \
        [&]() { static_cast<void>(expression); }, #expression, ::cpp_mastery::test::detail::FailureMode::abort_test, std::source_location::current())
