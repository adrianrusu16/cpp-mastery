#include <iostream>
#include <utility>
#include <vector>

struct Tracer {
    int id;

    explicit Tracer(const int id)
        : id{id}
    {
        std::cout << "construct        " << id << '\n';
    }

    Tracer(const Tracer& other)
        : id{other.id}
    {
        std::cout << "copy construct   " << id << '\n';
    }

    Tracer(Tracer&& other) noexcept
        : id{other.id}
    {
        other.id = -1;

        std::cout << "move construct   " << id << '\n';
    }

    Tracer& operator=(const Tracer& other)
    {
        std::cout
            << "copy assign      "
            << other.id
            << " -> "
            << id
            << '\n';

        id = other.id;

        return *this;
    }

    Tracer& operator=(Tracer&& other) noexcept
    {
        std::cout
            << "move assign      "
            << other.id
            << " -> "
            << id
            << '\n';

        id = other.id;
        other.id = -1;

        return *this;
    }

    ~Tracer()
    {
        std::cout << "destroy          " << id << '\n';
    }
};


static void test_scope()
{
    std::cout << "\n=== scope ===\n";

    Tracer a{1};

    {
        Tracer b{2};
    }

    std::cout << "leaving function\n";
}


static void test_copy()
{
    std::cout << "\n=== copy ===\n";

    Tracer a{10};

    Tracer b{a};

    Tracer c{30};
    c = a;
}


static void test_move()
{
    std::cout << "\n=== move ===\n";

    Tracer a{10};

    Tracer b{std::move(a)};

    Tracer c{30};
    c = std::move(b);
}


static void test_vector()
{
    std::cout << "\n=== vector ===\n";

    std::vector<Tracer> v;

    v.reserve(2);

    const std::size_t initial_capacity =
        v.capacity();

    for (std::size_t i = 0; i < initial_capacity; ++i) {
        v.emplace_back(static_cast<int>(i));
    }

    std::cout
        << "-- size == capacity: "
        << v.size()
        << " == "
        << v.capacity()
        << " --\n";

    std::cout << "-- forcing reallocation --\n";

    v.emplace_back(999);

    std::cout
        << "-- new capacity: "
        << v.capacity()
        << " --\n";
}


int main()
{
    test_scope();
    test_copy();
    test_move();
    test_vector();

    return 0;
}