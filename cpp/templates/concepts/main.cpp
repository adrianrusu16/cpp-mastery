#include <concepts>
#include <iostream>
#include <string>


template<typename T>
concept Addable = requires(T a, T b)
{
    { a + b } -> std::convertible_to<T>;
};


template<Addable T>
T add(T a, T b)
{
    return a + b;
}


template<typename T>
requires std::integral<T>
T multiply(T a, T b)
{
    return a * b;
}


struct Nothing
{
};

template<typename T>
T unconstrained_add(T a, T b)
{
    return a + b;
}


int main()
{
    Nothing a;
    Nothing b;

    unconstrained_add(a, b);

    std::cout << add(10, 20) << '\n';

    std::cout << add(
        std::string{"hello "},
        std::string{"world"}
    ) << '\n';

    std::cout << multiply(6, 7) << '\n';

    // Nothing a;
    // Nothing b;
    //
    // add(a, b);

    // multiply(1.5, 2.0);

    static_assert(Addable<int>);
    static_assert(Addable<std::string>);
    static_assert(!Addable<Nothing>);

    static_assert(std::integral<int>);
    static_assert(!std::integral<double>);

    return 0;
}