#pragma once

#include <cstdint>
#include <string>

namespace cpp_mastery::test {

struct TestFailure {
    std::string expression;
    std::string message;
    std::string file;
    std::string function;
    std::uint_least32_t line = 0;
    std::uint_least32_t column = 0;
};

class AbortTest final {
};

} // namespace cpp_mastery::test
