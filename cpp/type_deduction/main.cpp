#include <iostream>
#include <type_traits>
#include <utility>


template<typename T>
void print_type_info()
{
    std::cout
        << "const: "
        << std::is_const_v<std::remove_reference_t<T>>
        << ", lvalue ref: "
        << std::is_lvalue_reference_v<T>
        << ", rvalue ref: "
        << std::is_rvalue_reference_v<T>
        << '\n';
}


int main()
{
    int x = 10;
    const int cx = 20;

    int& rx = x;
    const int& crx = x;


    std::cout << "=== auto ===\n";

    auto a = x;
    auto b = cx;
    auto c = rx;
    auto d = crx;

    print_type_info<decltype(a)>();
    print_type_info<decltype(b)>();
    print_type_info<decltype(c)>();
    print_type_info<decltype(d)>();


    std::cout << "\n=== auto& ===\n";

    auto& e = x;
    auto& f = cx;

    print_type_info<decltype(e)>();
    print_type_info<decltype(f)>();


    std::cout << "\n=== auto&& ===\n";

    auto&& g = x;
    auto&& h = 30;

    print_type_info<decltype(g)>();
    print_type_info<decltype(h)>();


    std::cout << "\n=== decltype ===\n";

    print_type_info<decltype(x)>();
    print_type_info<decltype((x))>();
    print_type_info<decltype(std::move(x))>();


    std::cout << "\n=== const decltype ===\n";

    print_type_info<decltype(cx)>();
    print_type_info<decltype((cx))>();
    print_type_info<decltype(std::move(cx))>();

    return 0;
}