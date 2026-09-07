#include <iostream>
#include <utility>


struct Payload {
    Payload()
    {
        std::cout << "construct\n";
    }

    Payload(const Payload&)
    {
        std::cout << "COPY\n";
    }

    Payload(Payload&&) noexcept
    {
        std::cout << "MOVE\n";
    }
};

void consume(const Payload&)
{
    std::cout << "consume lvalue\n";
}

void consume(Payload&&)
{
    std::cout << "consume rvalue\n";
}

template<typename T>
void bad_forward(T&& value)
{
    consume(value);
}

template<typename T>
void good_forward(T&& value)
{
    consume(std::forward<T>(value));
}


struct Holder {
    Payload payload;

    explicit Holder(const Payload& p)
        : payload{p}
    {
    }

    explicit Holder(Payload&& p)
        : payload{std::move(p)}
    {
    }
};

template<typename T>
Holder make_holder_bad(T&& value)
{
    return Holder{value};
}

template<typename T>
Holder make_holder_good(T&& value)
{
    return Holder{std::forward<T>(value)};
}


void inspect(int&)
{
    std::cout << "int&\n";
}

void inspect(const int&)
{
    std::cout << "const int&\n";
}

void inspect(int&&)
{
    std::cout << "int&&\n";
}


template<typename T>
void bad_wrapper(T&& value)
{
    inspect(value);
}


template<typename T>
void forward_wrapper(T&& value)
{
    inspect(std::forward<T>(value));
}


int main()
{
    std::cout << "=== direct calls ===\n";

    int x = 10;
    const int cx = 20;

    int& lref = x;
    const int& cref = x;
    int&& rref = 30;

    inspect(x);
    inspect(cx);
    inspect(10);
    inspect(std::move(x));
    inspect(lref);
    inspect(cref);
    inspect(rref);
    inspect(std::move(rref));


    std::cout << "\n=== bad wrapper ===\n";

    bad_wrapper(x);
    bad_wrapper(10);
    bad_wrapper(std::move(x));


    std::cout << "\n=== forward wrapper ===\n";

    forward_wrapper(x);
    forward_wrapper(10);
    forward_wrapper(std::move(x));

    std::cout << "\n=== const move ===\n";

    const int value = 42;

    inspect(value);
    inspect(std::move(value));

    bad_wrapper(value);
    forward_wrapper(value);
    forward_wrapper(std::move(value));

    std::cout << "\n=== PAYLOAD ===\n";

    Payload p;

    bad_forward(p);
    bad_forward(Payload{});

    good_forward(p);
    good_forward(Payload{});

    std::cout << "\nBAD lvalue\n";
    auto a = make_holder_bad(p);

    std::cout << "\nBAD rvalue\n";
    auto b = make_holder_bad(Payload{});

    std::cout << "\nGOOD lvalue\n";
    auto c = make_holder_good(p);

    std::cout << "\nGOOD rvalue\n";
    auto d = make_holder_good(Payload{});

    return 0;
}