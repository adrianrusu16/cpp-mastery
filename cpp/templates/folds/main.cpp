#include <iostream>


template<typename... Args>
auto sum(Args... args)
{
    return (0 + ... + args);
}


template<typename... Args>
void print_lines(const Args&... args)
{
    ((std::cout << args << '\n'), ...);
}


template<typename... Args>
bool all(Args... args)
{
    return (... && args);
}


template<typename... Args>
auto subtract_left(Args... args)
{
    return (... - args);
}


template<typename... Args>
auto subtract_right(Args... args)
{
    return (args - ...);
}


int main()
{
    std::cout
        << "sum: "
        << sum(1, 2, 3, 4)
        << '\n';

    std::cout
        << "\nprint:\n";

    print_lines(
        42,
        "engine",
        3.14
    );

    std::cout
        << "\nall: "
        << all(true, true, false, true)
        << '\n';

    std::cout
        << "\nleft subtract: "
        << subtract_left(10, 3, 2)
        << '\n';

    std::cout
        << "right subtract: "
        << subtract_right(10, 3, 2)
        << '\n';
}