#pragma once

#include "TestResult.hpp"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace cpp_mastery::test {

struct RunSummary {
    std::size_t total = 0;
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t errors = 0;
    std::chrono::nanoseconds duration{};
};

class ConsoleReporter {
public:
    explicit ConsoleReporter(const bool verbose)
        : verbose_{verbose}
    {
    }

    void run_started(const std::string_view title) const
    {
        std::cout << title << "\n\n";
    }

    void suite_started(const std::string_view name) const
    {
        std::cout << name << '\n';
    }

    void test_finished(const TestResult& result) const
    {
        const char* label = "PASS";
        if (result.status() == TestStatus::failed) {
            label = "FAIL";
        } else if (result.status() == TestStatus::error) {
            label = "ERROR";
        }

        std::cout << "  [" << label << "] " << result.name;
        if (verbose_) {
            const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(result.duration);
            std::cout << " (" << micros.count() << " us)";
        }
        std::cout << '\n';

        for (const TestFailure& failure : result.failures) {
            std::cout << "      " << basename(failure.file) << ':' << failure.line << '\n'
                      << "      " << failure.expression << '\n'
                      << "      " << failure.message << '\n';
        }

        if (result.unexpected_exception.has_value()) {
            std::cout << "      unexpected exception: " << *result.unexpected_exception << '\n';
        }
    }

    void run_finished(const RunSummary& summary) const
    {
        std::cout << '\n'
                  << summary.total << " tests: " << summary.passed << " passed, "
                  << summary.failed << " failed, " << summary.errors << " errors";

        if (verbose_) {
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(summary.duration);
            std::cout << " (" << millis.count() << " ms)";
        }

        std::cout << '\n';
    }

private:
    [[nodiscard]] static std::string_view basename(const std::string_view path) noexcept
    {
        const std::size_t slash = path.find_last_of("/\\");
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

    bool verbose_ = false;
};

} // namespace cpp_mastery::test
