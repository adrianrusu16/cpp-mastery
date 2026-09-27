#pragma once

#include "TestCase.hpp"

#include <string>
#include <utility>
#include <vector>

namespace cpp_mastery::test {

class TestSuite {
public:
    explicit TestSuite(std::string name)
        : name_{std::move(name)}
    {
    }

    TestSuite& add(std::string name, const TestFunction function)
    {
        tests_.push_back(TestCase{std::move(name), function});
        return *this;
    }

    [[nodiscard]] const std::string& name() const noexcept
    {
        return name_;
    }

    [[nodiscard]] const std::vector<TestCase>& tests() const noexcept
    {
        return tests_;
    }

private:
    std::string name_;
    std::vector<TestCase> tests_;
};

} // namespace cpp_mastery::test
