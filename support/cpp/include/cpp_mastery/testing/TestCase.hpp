#pragma once

#include <string>

namespace cpp_mastery::test {

using TestFunction = void (*)();

struct TestCase {
    std::string name;
    TestFunction function = nullptr;
};

} // namespace cpp_mastery::test
