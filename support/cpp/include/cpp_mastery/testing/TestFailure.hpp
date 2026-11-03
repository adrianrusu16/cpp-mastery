#pragma once

#include <cstdint>
#include <string>

namespace cpp_mastery::test
{

struct TestFailure
{
    std::string expression;
    std::string message;
    std::string file;
    std::string function;
    std::uint_least32_t line = 0;
    std::uint_least32_t column = 0;
};

namespace detail
{
struct AbortTestAccess;
}

// A bare AbortTest is not an assertion failure. Only assertions use the trusted origin.
class AbortTest final
{
  public:
    AbortTest() noexcept = default;

    [[nodiscard]] bool from_assertion() const noexcept
    {
        return from_assertion_;
    }

  private:
    struct AssertionOrigin
    {
    };

    explicit AbortTest(AssertionOrigin) noexcept : from_assertion_{true} {}

    bool from_assertion_ = false;
    friend struct detail::AbortTestAccess;
};

namespace detail
{
struct AbortTestAccess final
{
    [[noreturn]] static void raise()
    {
        throw AbortTest{AbortTest::AssertionOrigin{}};
    }
};
} // namespace detail

} // namespace cpp_mastery::test
