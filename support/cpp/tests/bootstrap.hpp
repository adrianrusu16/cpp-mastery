#pragma once

#include <cstddef>
#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

namespace bootstrap {

class Checks {
public:
    void expect(const bool condition, const std::string_view description)
    {
        ++assertions_;
        if (!condition) {
            ++failures_;
            std::cerr << "  FAILED: " << description << '\n';
        }
    }

    template<typename Callable>
    void run_case(const std::string_view name, Callable&& callable)
    {
        const auto previous_failures = failures_;
        try {
            std::forward<Callable>(callable)(*this);
        } catch (const std::exception& error) {
            ++failures_;
            std::cerr << "  UNEXPECTED EXCEPTION: " << error.what() << '\n';
        } catch (...) {
            ++failures_;
            std::cerr << "  UNEXPECTED NON-STANDARD EXCEPTION\n";
        }

        std::cout << (failures_ == previous_failures ? "[PASS] " : "[FAIL] ") << name << '\n';
    }

    [[nodiscard]] std::size_t assertions() const noexcept { return assertions_; }
    [[nodiscard]] std::size_t failures() const noexcept { return failures_; }

private:
    std::size_t assertions_ = 0;
    std::size_t failures_ = 0;
};

} // namespace bootstrap
