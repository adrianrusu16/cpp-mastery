#include <iostream>
#include <string>
#include <utility>

template<typename T>
class Box {
public:
    explicit Box(T value)
        : value_{std::move(value)}
    {
    }

    T& get() noexcept
    {
        return value_;
    }

    const T& get() const noexcept
    {
        return value_;
    }

private:
    T value_;
};

int main()
{
    Box<int> a{10};
    Box<std::string> b{"hello"};

    std::cout << a.get() << '\n';
    std::cout << b.get() << '\n';

    b.get() = "world";

    std::cout << b.get() << '\n';
}