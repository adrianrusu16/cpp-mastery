#pragma once

#include "Assertions.hpp"
#include "Reporter.hpp"
#include "TestSuite.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cpp_mastery::test {

class Runner {
public:
    explicit Runner(std::string title)
        : title_{std::move(title)}
    {
    }

    Runner& add(TestSuite suite)
    {
        suites_.push_back(std::move(suite));
        return *this;
    }

    [[nodiscard]] int run(const int argc, char** argv) const
    {
        Options options;
        try {
            options = parse_options(argc, argv);
        } catch (const std::invalid_argument& error) {
            std::cerr << error.what() << "\n\n";
            print_usage(argv[0]);
            return 2;
        }

        if (options.help) {
            print_usage(argv[0]);
            return 0;
        }

        if (options.list) {
            list_tests(options);
            return 0;
        }

        ConsoleReporter reporter{options.verbose};
        reporter.run_started(title_);

        RunSummary summary;
        bool matched_any = false;
        const auto run_start = std::chrono::steady_clock::now();

        for (const TestSuite& suite : suites_) {
            if (!matches_suite(suite.name(), options.suite_filter)) {
                continue;
            }

            bool suite_started = false;

            for (const TestCase& test : suite.tests()) {
                if (!matches(test.name, options.test_filter)) {
                    continue;
                }

                matched_any = true;
                if (!suite_started) {
                    reporter.suite_started(suite.name());
                    suite_started = true;
                }

                TestResult result = execute(suite.name(), test);
                reporter.test_finished(result);

                ++summary.total;
                switch (result.status()) {
                    case TestStatus::passed:
                        ++summary.passed;
                        break;
                    case TestStatus::failed:
                        ++summary.failed;
                        break;
                    case TestStatus::error:
                        ++summary.errors;
                        break;
                }
            }
        }

        summary.duration = std::chrono::steady_clock::now() - run_start;

        if (!matched_any) {
            std::cerr << "No tests matched the selected filters.\n";
            return 2;
        }

        reporter.run_finished(summary);
        return summary.failed == 0 && summary.errors == 0 ? 0 : 1;
    }

private:
    struct Options {
        bool list = false;
        bool verbose = false;
        bool help = false;
        std::optional<std::string> suite_filter;
        std::optional<std::string> test_filter;
    };

    class ActiveResultGuard {
    public:
        explicit ActiveResultGuard(TestResult& result)
            : previous_{detail::active_result}
        {
            detail::active_result = &result;
        }

        ~ActiveResultGuard()
        {
            detail::active_result = previous_;
        }

        ActiveResultGuard(const ActiveResultGuard&) = delete;
        ActiveResultGuard& operator=(const ActiveResultGuard&) = delete;

    private:
        TestResult* previous_;
    };

    [[nodiscard]] static TestResult execute(
        const std::string& suite_name,
        const TestCase& test)
    {
        TestResult result;
        result.suite = suite_name;
        result.name = test.name;

        const auto start = std::chrono::steady_clock::now();
        ActiveResultGuard guard{result};

        try {
            if (test.function == nullptr) {
                throw std::logic_error{"test case has no function"};
            }
            test.function();
        } catch (const AbortTest&) {
            // REQUIRE intentionally aborts only the current test.
        } catch (const std::exception& error) {
            result.unexpected_exception = error.what();
        } catch (...) {
            result.unexpected_exception = "unknown non-standard exception";
        }

        result.duration = std::chrono::steady_clock::now() - start;
        return result;
    }

    [[nodiscard]] static Options parse_options(const int argc, char** argv)
    {
        Options options;

        for (int index = 1; index < argc; ++index) {
            const std::string_view argument{argv[index]};

            if (argument == "--list") {
                options.list = true;
            } else if (argument == "--verbose") {
                options.verbose = true;
            } else if (argument == "--help" || argument == "-h") {
                options.help = true;
            } else if (argument == "--suite") {
                if (++index >= argc) {
                    throw std::invalid_argument{"--suite requires a value"};
                }
                options.suite_filter = argv[index];
            } else if (argument == "--test") {
                if (++index >= argc) {
                    throw std::invalid_argument{"--test requires a value"};
                }
                options.test_filter = argv[index];
            } else {
                throw std::invalid_argument{"unknown test-runner option: " + std::string{argument}};
            }
        }

        return options;
    }

    static void print_usage(const std::string_view executable)
    {
        std::cout << "Usage: " << executable << " [options]\n"
                  << "  --list            list matching suites and tests\n"
                  << "  --suite <name>    run one named suite\n"
                  << "  --test <text>     run tests whose names contain <text>\n"
                  << "  --verbose         include timing information\n"
                  << "  --help, -h        show this help\n";
    }

    void list_tests(const Options& options) const
    {
        bool matched_any = false;
        for (const TestSuite& suite : suites_) {
            if (!matches_suite(suite.name(), options.suite_filter)) {
                continue;
            }

            bool printed_suite = false;
            for (const TestCase& test : suite.tests()) {
                if (!matches(test.name, options.test_filter)) {
                    continue;
                }

                if (!printed_suite) {
                    std::cout << suite.name() << '\n';
                    printed_suite = true;
                }

                std::cout << "  " << test.name << '\n';
                matched_any = true;
            }
        }

        if (!matched_any) {
            std::cout << "No tests matched the selected filters.\n";
        }
    }

    [[nodiscard]] static bool matches_suite(
        const std::string_view value,
        const std::optional<std::string>& filter)
    {
        if (!filter.has_value()) {
            return true;
        }
        return lowercase(value) == lowercase(*filter);
    }

    [[nodiscard]] static bool matches(
        const std::string_view value,
        const std::optional<std::string>& filter)
    {
        if (!filter.has_value()) {
            return true;
        }

        const std::string lowered_value = lowercase(value);
        const std::string lowered_filter = lowercase(*filter);
        return lowered_value.find(lowered_filter) != std::string::npos;
    }

    [[nodiscard]] static std::string lowercase(const std::string_view value)
    {
        std::string result{value};
        std::ranges::transform(result, result.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return result;
    }

    std::string title_;
    std::vector<TestSuite> suites_;
};

} // namespace cpp_mastery::test
