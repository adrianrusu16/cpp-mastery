#include <cpp_mastery/vector/Vector.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <string>

namespace {

struct alignas(64) CacheLineData {
    int values[16]{};
};

void demonstrate_alias_safe_growth()
{
    cpp_mastery::Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");
    values.push_back("three");

    values.push_back(values.front());

    std::cout << "alias-safe growth: ";
    for (const auto& value : values) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
}

void demonstrate_ranges()
{
    cpp_mastery::Vector<int> values;
    values.push_back(40);
    values.push_back(10);
    values.push_back(30);
    values.push_back(20);

    std::ranges::sort(values);

    std::cout << "ranges::sort: ";
    for (const int value : values) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
}

void demonstrate_alignment()
{
    cpp_mastery::Vector<CacheLineData> values;
    values.resize(4);

    std::cout << "alignof(CacheLineData): " << alignof(CacheLineData) << '\n';
    std::cout << "element address mod 64: ";

    for (const auto& value : values) {
        const auto address = reinterpret_cast<std::uintptr_t>(&value);
        std::cout << (address % 64U) << ' ';
    }
    std::cout << '\n';
}

} // namespace

int main()
{
    demonstrate_alias_safe_growth();
    demonstrate_ranges();
    demonstrate_alignment();
    return 0;
}
