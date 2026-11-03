#pragma once

#include "TestResult.hpp"

namespace cpp_mastery::test::detail
{

// The active assertion result is local to the current thread.
inline thread_local TestResult* active_result = nullptr;

// Restores the previous test context even when the test throws.
class ActiveResultGuard final
{
  public:
    explicit ActiveResultGuard(TestResult& result) noexcept : previous_{active_result}
    {
        active_result = &result;
    }

    ~ActiveResultGuard() noexcept
    {
        active_result = previous_;
    }

    ActiveResultGuard(const ActiveResultGuard&) = delete;
    ActiveResultGuard& operator=(const ActiveResultGuard&) = delete;
    ActiveResultGuard(ActiveResultGuard&&) = delete;
    ActiveResultGuard& operator=(ActiveResultGuard&&) = delete;

  private:
    TestResult* previous_;
};

} // namespace cpp_mastery::test::detail
