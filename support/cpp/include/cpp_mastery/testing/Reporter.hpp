#pragma once

#include "TestResult.hpp"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace cpp_mastery::test
{

struct RunSummary
{
    std::size_t total = 0;
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t errors = 0;
    std::chrono::nanoseconds duration{};
};

class ConsoleReporter
{
  public:
    explicit ConsoleReporter(const bool verbose, std::ostream& output = std::cout)
        : verbose_{verbose}, output_{output}
    {
    }

    void run_started(const std::string_view title) const
    {
        output_ << title << "\n\n";
    }

    void suite_started(const std::string_view name) const
    {
        output_ << name << '\n';
    }

    void test_finished(const TestResult& result) const
    {
        const char* label = "PASS";
        if (result.status() == TestStatus::failed)
        {
            label = "FAIL";
        }
        else if (result.status() == TestStatus::error)
        {
            label = "ERROR";
        }

        output_ << "  [" << label << "] " << result.name;
        if (verbose_)
        {
            const auto micros =
                std::chrono::duration_cast<std::chrono::microseconds>(result.duration);
            output_ << " (" << micros.count() << " us)";
        }
        output_ << '\n';

        for (const TestFailure& failure : result.failures)
        {
            output_ << "      " << basename(failure.file) << ':' << failure.line << '\n'
                    << "      " << failure.expression << '\n'
                    << "      " << failure.message << '\n';
        }

        if (result.unexpected_exception.has_value())
        {
            output_ << "      unexpected exception: " << *result.unexpected_exception << '\n';
        }
    }

    void run_finished(const RunSummary& summary) const
    {
        output_ << '\n'
                << summary.total << (summary.total == 1 ? " test: " : " tests: ") << summary.passed
                << " passed, " << summary.failed << " failed, " << summary.errors
                << (summary.errors == 1 ? " error" : " errors");

        if (verbose_)
        {
            const auto millis =
                std::chrono::duration_cast<std::chrono::milliseconds>(summary.duration);
            output_ << " (" << millis.count() << " ms)";
        }

        output_ << '\n';
    }

  private:
    [[nodiscard]] static std::string_view basename(const std::string_view path) noexcept
    {
        const std::size_t slash = path.find_last_of("/\\");
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

    bool verbose_ = false;
    std::ostream& output_;
};

} // namespace cpp_mastery::test
