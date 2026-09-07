#include <iostream>
#include <string>
#include <type_traits>


template<typename T>
void by_value(T)
{
    if constexpr (std::is_const_v<T>) {
        std::cout << "T is const\n";
    } else {
        std::cout << "T is non-const\n";
    }
}


template<typename T>
void by_reference(T&)
{
    if constexpr (std::is_const_v<T>) {
        std::cout << "T is const\n";
    } else {
        std::cout << "T is non-const\n";
    }
}


template<typename T>
void by_const_reference(const T&)
{
    if constexpr (std::is_const_v<T>) {
        std::cout << "T is const\n";
    } else {
        std::cout << "T is non-const\n";
    }
}


template<typename T>
void forwarding_reference(T&&)
{
    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "T is lvalue reference\n";
    } else {
        std::cout << "T is not lvalue reference\n";
    }
}


int main()
{
    int x = 10;
    const int cx = 20;

    std::cout << "=== by value ===\n";

    by_value(x);
    by_value(cx);
    by_value(30);


    std::cout << "\n=== by reference ===\n";

    by_reference(x);
    by_reference(cx);


    std::cout << "\n=== by const reference ===\n";

    by_const_reference(x);
    by_const_reference(cx);


    std::cout << "\n=== forwarding reference ===\n";

    forwarding_reference(x);
    forwarding_reference(cx);
    forwarding_reference(30);

    return 0;
}
