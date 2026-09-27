#pragma once

#include "TestFailure.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace cpp_mastery::test {

enum class TestStatus {
    passed,
    failed,
    error,
};

struct TestResult {
    std::string suite;
    std::string name;
    std::vector<TestFailure> failures;
    std::optional<std::string> unexpected_exception;
    std::chrono::nanoseconds duration{};

    [[nodiscard]] TestStatus status() const noexcept
    {
        if (unexpected_exception.has_value()) {
            return TestStatus::error;
        }
        return failures.empty() ? TestStatus::passed : TestStatus::failed;
    }

    [[nodiscard]] bool passed() const noexcept
    {
        return status() == TestStatus::passed;
    }
};

} // namespace cpp_mastery::test
