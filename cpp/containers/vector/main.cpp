#include "Vector.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <stdexcept>
#include <ranges>

static_assert(
    std::ranges::range<Vector<int> >
);

static_assert(
    std::ranges::sized_range<Vector<int> >
);

static_assert(
    std::ranges::common_range<Vector<int> >
);

static_assert(
    std::ranges::random_access_range<Vector<int> >
);

static_assert(
    std::ranges::contiguous_range<Vector<int> >
);


struct alignas(64) CacheLineData {
    int values[16]{};
};


struct SmallData {
    int value{};
};


struct PopBackProbe {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;

    int value;

    explicit PopBackProbe(const int value)
        : value{value} {
        ++live_count;
    }

    PopBackProbe(const PopBackProbe &other)
        : value{other.value} {
        ++live_count;
    }

    PopBackProbe(PopBackProbe &&other) noexcept
        : value{other.value} {
        ++live_count;
    }

    ~PopBackProbe() {
        --live_count;
        ++destructor_calls;
    }
};


struct ResizeProbe {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;

    int value;

    ResizeProbe()
        : value{0} {
        ++live_count;
    }

    explicit ResizeProbe(const int value)
        : value{value} {
        ++live_count;
    }

    ResizeProbe(const ResizeProbe &other)
        : value{other.value} {
        ++live_count;
    }

    ResizeProbe(ResizeProbe &&other) noexcept
        : value{other.value} {
        ++live_count;
    }

    ~ResizeProbe() {
        --live_count;
        ++destructor_calls;
    }
};


struct ThrowOnDefault {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;
    static inline int defaults_before_throw = -1;

    int value = 0;

    ThrowOnDefault() {
        if (defaults_before_throw == 0) {
            throw std::runtime_error{
                "intentional default construction failure"
            };
        }

        if (defaults_before_throw > 0) {
            --defaults_before_throw;
        }

        ++live_count;
    }

    explicit ThrowOnDefault(const int value)
        : value{value} {
        ++live_count;
    }

    ThrowOnDefault(const ThrowOnDefault &other)
        : value{other.value} {
        ++live_count;
    }

    ThrowOnDefault(ThrowOnDefault &&other) noexcept
        : value{other.value} {
        ++live_count;
    }

    ~ThrowOnDefault() {
        --live_count;
        ++destructor_calls;
    }
};


struct ResizeRelocationProbe {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;
    static inline int copies = 0;
    static inline int moves = 0;
    static inline int copies_before_throw = -1;

    int value = 0;

    ResizeRelocationProbe() {
        ++live_count;
    }

    explicit ResizeRelocationProbe(const int value)
        : value{value} {
        ++live_count;
    }

    ResizeRelocationProbe(
        const ResizeRelocationProbe &other
    )
        : value{other.value} {
        if (copies_before_throw == 0) {
            throw std::runtime_error{
                "intentional relocation copy failure"
            };
        }

        if (copies_before_throw > 0) {
            --copies_before_throw;
        }

        ++copies;
        ++live_count;
    }

    /*
     * Deliberately NOT noexcept.
     *
     * Because copying is available,
     * move_if_noexcept should prefer copying.
     */
    ResizeRelocationProbe(
        ResizeRelocationProbe &&other
    )
        : value{other.value} {
        ++moves;
        ++live_count;

        other.value = -1;
    }

    ~ResizeRelocationProbe() {
        --live_count;
        ++destructor_calls;
    }
};


struct ResizeFillProbe {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;
    static inline int copies = 0;
    static inline int copies_before_throw = -1;

    int value = 0;

    ResizeFillProbe() {
        ++live_count;
    }

    explicit ResizeFillProbe(const int value)
        : value{value} {
        ++live_count;
    }

    ResizeFillProbe(const ResizeFillProbe &other)
        : value{other.value} {
        if (copies_before_throw == 0) {
            throw std::runtime_error{
                "intentional fill copy failure"
            };
        }

        if (copies_before_throw > 0) {
            --copies_before_throw;
        }

        ++copies;
        ++live_count;
    }

    ResizeFillProbe(ResizeFillProbe &&other) noexcept
        : value{other.value} {
        other.value = -1;
        ++live_count;
    }

    ~ResizeFillProbe() {
        --live_count;
        ++destructor_calls;
    }
};


template<typename T>
void print_addresses(
    const Vector<T> &vector,
    const std::size_t modulus) {
    if (vector.empty()) {
        return;
    }

    const auto base =
            reinterpret_cast<std::uintptr_t>(
                &vector[0]
            );

    for (std::size_t i = 0;
         i < vector.size();
         ++i) {
        const auto address =
                reinterpret_cast<std::uintptr_t>(
                    &vector[i]
                );

        std::cout
                << "v[" << i << "] = "
                << static_cast<const void *>(
                    &vector[i]
                )
                << ", offset = "
                << (address - base)
                << ", address % "
                << modulus
                << " = "
                << (address % modulus)
                << '\n';
    }
}


void test_self_copy() {
    std::cout
            << "=== SELF COPY WITH REALLOCATION ===\n";

    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");
    v.push_back("three");

    std::cout
            << "before: size = "
            << v.size()
            << ", capacity = "
            << v.capacity()
            << '\n';

    /*
     * v[0] aliases the vector's own allocation.
     *
     * Because size == capacity, this forces growth.
     */
    v.push_back(v[0]);

    for (std::size_t i = 0;
         i < v.size();
         ++i) {
        std::cout
                << i
                << ": "
                << v[i]
                << '\n';
    }
}


void test_self_move() {
    std::cout
            << "\n=== SELF MOVE WITH REALLOCATION ===\n";

    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");
    v.push_back("three");

    /*
     * Again size == capacity.
     *
     * This time we explicitly allow v[0]
     * to be moved from.
     */
    v.push_back(
        std::move(v[0])
    );

    for (std::size_t i = 0;
         i < v.size();
         ++i) {
        std::cout
                << i
                << ": "
                << v[i]
                << '\n';
    }
}


void test_alignment() {
    std::cout
            << "\n=== TYPE LAYOUT ===\n";

    std::cout
            << "sizeof(CacheLineData): "
            << sizeof(CacheLineData)
            << '\n';

    std::cout
            << "alignof(CacheLineData): "
            << alignof(CacheLineData)
            << '\n';

    std::cout
            << "sizeof(SmallData): "
            << sizeof(SmallData)
            << '\n';

    std::cout
            << "alignof(SmallData): "
            << alignof(SmallData)
            << '\n';


    std::cout
            << "\n=== CACHE LINE DATA ADDRESSES ===\n";

    Vector<CacheLineData> cache_data;

    for (int i = 0; i < 4; ++i) {
        cache_data.push_back(
            CacheLineData{}
        );
    }

    print_addresses(
        cache_data,
        64
    );


    std::cout
            << "\n=== SMALL DATA ADDRESSES ===\n";

    Vector<SmallData> small_data;

    for (int i = 0; i < 8; ++i) {
        small_data.push_back(
            SmallData{i}
        );
    }

    print_addresses(
        small_data,
        64
    );
}


bool test_copy_constructor() {
    Vector<std::string> original;

    original.push_back("zero");
    original.push_back("one");
    original.push_back("two");

    /*
     * Deliberately make capacity larger than size.
     *
     * We decided that a copy should copy only
     * the logical elements, not spare capacity.
     */
    original.reserve(8);

    Vector<std::string> copy{original};

    if (original.size() != 3) {
        return false;
    }

    if (original.capacity() != 8) {
        return false;
    }

    if (copy.size() != 3) {
        return false;
    }

    /*
     * Our chosen copy semantics:
     *
     * size     = original.size()
     * capacity = original.size()
     */
    if (copy.capacity() != 3) {
        return false;
    }

    if (copy[0] != "zero"
        || copy[1] != "one"
        || copy[2] != "two") {
        return false;
    }

    /*
     * Verify deep-copy ownership.
     */
    copy[0] = "changed";

    if (copy[0] != "changed") {
        return false;
    }

    if (original[0] != "zero") {
        return false;
    }

    return true;
}


struct ThrowOnCopy {
    static inline int live_count = 0;
    static inline int copies_before_throw = -1;

    int value = 0;


    explicit ThrowOnCopy(const int value)
        : value{value} {
        ++live_count;
    }


    ThrowOnCopy(const ThrowOnCopy &other)
        : value{other.value} {
        if (copies_before_throw == 0) {
            throw std::runtime_error{
                "intentional copy failure"
            };
        }

        if (copies_before_throw > 0) {
            --copies_before_throw;
        }

        ++live_count;
    }


    ThrowOnCopy(ThrowOnCopy &&other) noexcept
        : value{other.value} {
        other.value = -1;
        ++live_count;
    }


    ~ThrowOnCopy() {
        --live_count;
    }
};


bool test_copy_constructor_exception_safety() {
    if (ThrowOnCopy::live_count != 0) {
        return false;
    }

    bool result = true;

    {
        Vector<ThrowOnCopy> original;

        original.push_back(ThrowOnCopy{10});
        original.push_back(ThrowOnCopy{20});
        original.push_back(ThrowOnCopy{30});
        original.push_back(ThrowOnCopy{40});

        original.reserve(8);

        if (ThrowOnCopy::live_count != 4) {
            return false;
        }

        const int live_before_copy =
                ThrowOnCopy::live_count;

        /*
         * Copy element 0 successfully.
         * Copy element 1 successfully.
         * Throw while copying element 2.
         */
        ThrowOnCopy::copies_before_throw = 2;

        bool exception_caught = false;

        try {
            Vector<ThrowOnCopy> copy{original};
        } catch (const std::runtime_error &) {
            exception_caught = true;
        }

        ThrowOnCopy::copies_before_throw = -1;

        if (!exception_caught) {
            result = false;
        }

        /*
         * All partially constructed objects in the
         * failed copy must have been destroyed.
         */
        if (ThrowOnCopy::live_count != live_before_copy) {
            result = false;
        }

        /*
         * The source must remain unchanged.
         */
        if (original.size() != 4) {
            result = false;
        }

        if (original[0].value != 10
            || original[1].value != 20
            || original[2].value != 30
            || original[3].value != 40) {
            result = false;
        }
    }

    /*
     * original has now also been destroyed.
     *
     * If this is non-zero, something leaked a live object.
     */
    if (ThrowOnCopy::live_count != 0) {
        result = false;
    }

    return result;
}


bool test_copy_assignment() {
    Vector<std::string> source;

    source.push_back("zero");
    source.push_back("one");
    source.push_back("two");

    source.reserve(8);

    Vector<std::string> destination;

    destination.push_back("old-a");
    destination.push_back("old-b");

    destination.reserve(16);

    destination = source;

    /*
     * Source must be untouched.
     */
    if (source.size() != 3) {
        return false;
    }

    if (source.capacity() != 8) {
        return false;
    }

    if (source[0] != "zero"
        || source[1] != "one"
        || source[2] != "two") {
        return false;
    }

    /*
     * Assignment replaces destination's logical value.
     *
     * Since our copy constructor copies size only,
     * copy-and-swap will eventually give this
     * destination capacity == source.size().
     */
    if (destination.size() != 3) {
        return false;
    }

    if (destination.capacity() != 3) {
        return false;
    }

    if (destination[0] != "zero"
        || destination[1] != "one"
        || destination[2] != "two") {
        return false;
    }

    /*
     * Deep-copy independence.
     */
    destination[0] = "changed";

    if (source[0] != "zero") {
        return false;
    }

    if (destination[0] != "changed") {
        return false;
    }

    return true;
}


bool test_self_copy_assignment() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");

    v.reserve(8);

    const std::size_t old_size = v.size();
    const std::size_t old_capacity = v.capacity();

    const Vector<std::string> &self = v;
    v = self;

    if (v.size() != old_size) {
        return false;
    }

    if (v.capacity() != old_capacity) {
        return false;
    }

    if (v[0] != "zero"
        || v[1] != "one"
        || v[2] != "two") {
        return false;
    }

    return true;
}


bool test_copy_assignment_exception_safety() {
    if (ThrowOnCopy::live_count != 0) {
        return false;
    }

    bool result = true;

    {
        Vector<ThrowOnCopy> source;

        source.push_back(ThrowOnCopy{10});
        source.push_back(ThrowOnCopy{20});
        source.push_back(ThrowOnCopy{30});
        source.push_back(ThrowOnCopy{40});

        source.reserve(8);


        Vector<ThrowOnCopy> destination;

        destination.push_back(ThrowOnCopy{100});
        destination.push_back(ThrowOnCopy{200});

        destination.reserve(16);


        const std::size_t old_size =
                destination.size();

        const std::size_t old_capacity =
                destination.capacity();

        const int live_before_assignment =
                ThrowOnCopy::live_count;


        /*
         * Copy two elements successfully, then throw
         * while constructing the third one.
         */
        ThrowOnCopy::copies_before_throw = 2;

        bool exception_caught = false;

        try {
            destination = source;
        } catch (const std::runtime_error &) {
            exception_caught = true;
        }

        ThrowOnCopy::copies_before_throw = -1;


        if (!exception_caught) {
            result = false;
        }


        /*
         * No objects from the failed temporary copy
         * may remain alive.
         */
        if (ThrowOnCopy::live_count
            != live_before_assignment) {
            result = false;
        }


        /*
         * Source must remain unchanged.
         */
        if (source.size() != 4
            || source.capacity() != 8) {
            result = false;
        }

        if (source[0].value != 10
            || source[1].value != 20
            || source[2].value != 30
            || source[3].value != 40) {
            result = false;
        }


        /*
         * Strong exception guarantee:
         *
         * destination must be EXACTLY as it was
         * before the failed assignment.
         */
        if (destination.size() != old_size) {
            result = false;
        }

        if (destination.capacity() != old_capacity) {
            result = false;
        }

        if (destination[0].value != 100
            || destination[1].value != 200) {
            result = false;
        }
    }


    /*
     * Both source and destination have now died.
     */
    if (ThrowOnCopy::live_count != 0) {
        result = false;
    }

    return result;
}


bool test_move_constructor() {
    Vector<std::string> source;

    source.push_back("zero");
    source.push_back("one");
    source.push_back("two");

    source.reserve(8);

    const std::size_t old_size =
            source.size();

    const std::size_t old_capacity =
            source.capacity();

    /*
     * Save the address of the actual allocation.
     *
     * A true O(1) move constructor should transfer
     * this allocation rather than allocate/move
     * individual strings.
     */
    const std::string *old_data =
            &source[0];


    Vector<std::string> destination{
        std::move(source)
    };


    /*
     * Destination receives the entire allocation.
     */
    if (destination.size() != old_size) {
        return false;
    }

    if (destination.capacity() != old_capacity) {
        return false;
    }

    if (destination[0] != "zero"
        || destination[1] != "one"
        || destination[2] != "two") {
        return false;
    }


    /*
     * Strong evidence that we stole the allocation
     * instead of relocating individual elements.
     */
    if (&destination[0] != old_data) {
        return false;
    }


    /*
     * Source must no longer own that allocation.
     */
    if (!source.empty()) {
        return false;
    }

    if (source.size() != 0) {
        return false;
    }

    if (source.capacity() != 0) {
        return false;
    }


    /*
     * Moved-from object should remain usable.
     */
    source.push_back("reused");

    if (source.size() != 1) {
        return false;
    }

    if (source[0] != "reused") {
        return false;
    }

    return true;
}


bool test_move_assignment() {
    Vector<std::string> source;

    source.push_back("zero");
    source.push_back("one");
    source.push_back("two");

    source.reserve(8);

    const std::size_t source_size =
            source.size();

    const std::size_t source_capacity =
            source.capacity();

    const std::string *source_data =
            &source[0];


    Vector<std::string> destination;

    destination.push_back("old-a");
    destination.push_back("old-b");

    destination.reserve(16);


    destination = std::move(source);


    /*
     * Destination should steal source's exact allocation.
     */
    if (destination.size() != source_size) {
        return false;
    }

    if (destination.capacity() != source_capacity) {
        return false;
    }

    if (&destination[0] != source_data) {
        return false;
    }

    if (destination[0] != "zero"
        || destination[1] != "one"
        || destination[2] != "two") {
        return false;
    }


    /*
     * Source becomes our chosen moved-from state:
     * equivalent to an empty vector.
     */
    if (!source.empty()) {
        return false;
    }

    if (source.size() != 0) {
        return false;
    }

    if (source.capacity() != 0) {
        return false;
    }


    /*
     * Source remains reusable.
     */
    source.push_back("reused");

    if (source.size() != 1
        || source[0] != "reused") {
        return false;
    }

    return true;
}


bool test_self_move_assignment() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");

    v.reserve(8);

    const std::size_t old_size =
            v.size();

    const std::size_t old_capacity =
            v.capacity();

    const std::string *old_data =
            &v[0];

    /*
     * Avoid Clang's explicit self-move warning while
     * still making source and destination the same object.
     */
    Vector<std::string> &self = v;

    v = std::move(self);

    if (v.size() != old_size) {
        return false;
    }

    if (v.capacity() != old_capacity) {
        return false;
    }

    if (&v[0] != old_data) {
        return false;
    }

    if (v[0] != "zero"
        || v[1] != "one"
        || v[2] != "two") {
        return false;
    }

    return true;
}


struct EmplaceProbe {
    static inline int direct_constructions = 0;
    static inline int copies = 0;
    static inline int moves = 0;

    int id;
    std::string name;

    EmplaceProbe(
        const int id,
        std::string name
    )
        : id{id},
          name{std::move(name)} {
        ++direct_constructions;
    }

    EmplaceProbe(const EmplaceProbe &other)
        : id{other.id},
          name{other.name} {
        ++copies;
    }

    EmplaceProbe(EmplaceProbe &&other) noexcept
        : id{other.id},
          name{std::move(other.name)} {
        ++moves;
    }
};


bool test_emplace_back() {
    EmplaceProbe::direct_constructions = 0;
    EmplaceProbe::copies = 0;
    EmplaceProbe::moves = 0;

    Vector<EmplaceProbe> v;

    EmplaceProbe &element =
            v.emplace_back(
                42,
                std::string{"engine"}
            );

    if (v.size() != 1) {
        return false;
    }

    if (element.id != 42
        || element.name != "engine") {
        return false;
    }

    /*
     * emplace_back should return the actual object
     * stored inside the vector.
     */
    if (&element != &v[0]) {
        return false;
    }

    /*
     * There was no EmplaceProbe object before the call.
     * It should have been constructed directly in
     * Vector storage.
     */
    if (EmplaceProbe::direct_constructions != 1) {
        return false;
    }

    if (EmplaceProbe::copies != 0) {
        return false;
    }

    if (EmplaceProbe::moves != 0) {
        return false;
    }

    return true;
}


bool test_push_back_self_copy_with_reallocation() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");
    v.push_back("three");

    /*
     * With our growth strategy:
     *
     * size     = 4
     * capacity = 4
     *
     * So the next insertion must reallocate.
     */
    if (v.size() != 4 || v.capacity() != 4) {
        return false;
    }

    /*
     * v[0] refers into the allocation that push_back()
     * is about to replace.
     */
    v.push_back(v[0]);

    if (v.size() != 5) {
        return false;
    }

    if (v.capacity() != 8) {
        return false;
    }

    /*
     * Copy semantics:
     *
     * the original element must remain unchanged,
     * and the new element must contain its value.
     */
    if (v[0] != "zero"
        || v[1] != "one"
        || v[2] != "two"
        || v[3] != "three"
        || v[4] != "zero") {
        return false;
    }

    return true;
}


bool test_push_back_self_move_with_reallocation() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");
    v.push_back("three");

    if (v.size() != 4 || v.capacity() != 4) {
        return false;
    }

    /*
     * Explicitly permit the new element to consume
     * the value currently stored in v[0].
     */
    v.push_back(std::move(v[0]));

    if (v.size() != 5) {
        return false;
    }

    if (v.capacity() != 8) {
        return false;
    }

    /*
     * Do NOT assert that v[0] is empty.
     *
     * It is moved-from, so its exact value is not
     * something our test should depend on.
     *
     * We only verify that the value reached the newly
     * appended element and that the unrelated elements
     * survived correctly.
     */
    if (v[1] != "one"
        || v[2] != "two"
        || v[3] != "three"
        || v[4] != "zero") {
        return false;
    }

    return true;
}


bool test_at_valid_mutable_access() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");

    std::string &element = v.at(1);

    if (element != "one") {
        return false;
    }

    /*
     * Prove that at() returned a reference to
     * the actual stored element, not a copy.
     */
    element = "changed";

    if (v[1] != "changed") {
        return false;
    }

    return true;
}


bool test_at_out_of_range() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");

    bool exception_caught = false;

    try {
        v.at(3);
    } catch (const std::out_of_range &) {
        exception_caught = true;
    }

    return exception_caught;
}


bool test_at_empty_vector() {
    Vector<int> v;

    try {
        v.at(0);
    } catch (const std::out_of_range &) {
        return true;
    }

    return false;
}


bool test_at_const_access() {
    Vector<std::string> mutable_vector;

    mutable_vector.push_back("zero");
    mutable_vector.push_back("one");
    mutable_vector.push_back("two");

    const Vector<std::string> &v =
            mutable_vector;

    const std::string &element =
            v.at(1);

    return element == "one";
}


bool test_front_mutable() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");

    std::string &first = v.front();

    if (first != "zero") {
        return false;
    }

    first = "changed";

    if (v[0] != "changed") {
        return false;
    }

    /*
     * Prove this is literally the first stored object.
     */
    if (&first != &v[0]) {
        return false;
    }

    return true;
}


bool test_front_const() {
    Vector<std::string> mutable_vector;

    mutable_vector.push_back("zero");
    mutable_vector.push_back("one");
    mutable_vector.push_back("two");

    const Vector<std::string> &v =
            mutable_vector;

    const std::string &first =
            v.front();

    if (first != "zero") {
        return false;
    }

    if (&first != &v[0]) {
        return false;
    }

    return true;
}


bool test_back_mutable() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");

    std::string &last = v.back();

    if (last != "two") {
        return false;
    }

    last = "changed";

    if (v[2] != "changed") {
        return false;
    }

    if (&last != &v[2]) {
        return false;
    }

    return true;
}


bool test_back_const() {
    Vector<std::string> mutable_vector;

    mutable_vector.push_back("zero");
    mutable_vector.push_back("one");
    mutable_vector.push_back("two");

    const Vector<std::string> &v =
            mutable_vector;

    const std::string &last =
            v.back();

    if (last != "two") {
        return false;
    }

    if (&last != &v[2]) {
        return false;
    }

    return true;
}


bool test_data_mutable() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");

    std::string *ptr = v.data();

    if (ptr != &v[0]) {
        return false;
    }

    if (ptr[0] != "zero"
        || ptr[1] != "one"
        || ptr[2] != "two") {
        return false;
    }

    /*
     * Prove that this is a mutable pointer
     * into the actual contiguous storage.
     */
    ptr[1] = "changed";

    if (v[1] != "changed") {
        return false;
    }

    return true;
}


bool test_data_const() {
    Vector<std::string> mutable_vector;

    mutable_vector.push_back("zero");
    mutable_vector.push_back("one");
    mutable_vector.push_back("two");

    const Vector<std::string> &v =
            mutable_vector;

    const std::string *ptr =
            v.data();

    if (ptr != &v[0]) {
        return false;
    }

    if (ptr[0] != "zero"
        || ptr[1] != "one"
        || ptr[2] != "two") {
        return false;
    }

    return true;
}


bool test_begin_end_mutable() {
    Vector<int> v;

    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    int *first = v.begin();
    int *last = v.end();

    if (first != v.data()) {
        return false;
    }

    if (last != v.data() + v.size()) {
        return false;
    }

    if (last - first != 3) {
        return false;
    }

    if (*first != 10) {
        return false;
    }

    *(first + 1) = 99;

    if (v[1] != 99) {
        return false;
    }

    return true;
}


bool test_begin_end_empty() {
    Vector<int> v;

    if (v.begin() != nullptr) {
        return false;
    }

    if (v.end() != nullptr) {
        return false;
    }

    if (v.begin() != v.end()) {
        return false;
    }

    return true;
}


bool test_begin_end_const() {
    Vector<int> mutable_vector;

    mutable_vector.push_back(10);
    mutable_vector.push_back(20);
    mutable_vector.push_back(30);

    const Vector<int> &v =
            mutable_vector;

    const int *first = v.begin();
    const int *last = v.end();

    if (first != v.data()) {
        return false;
    }

    if (last != v.data() + v.size()) {
        return false;
    }

    if (last - first != 3) {
        return false;
    }

    if (*first != 10) {
        return false;
    }

    return true;
}


bool test_range_based_for() {
    Vector<int> v;

    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    int sum = 0;

    for (const int value: v) {
        sum += value;
    }

    return sum == 60;
}


bool test_std_sort() {
    Vector<int> v;

    v.push_back(40);
    v.push_back(10);
    v.push_back(30);
    v.push_back(20);

    std::ranges::sort(v);

    return v[0] == 10
           && v[1] == 20
           && v[2] == 30
           && v[3] == 40;
}


bool test_const_range_based_for() {
    Vector<int> mutable_vector;

    mutable_vector.push_back(10);
    mutable_vector.push_back(20);
    mutable_vector.push_back(30);

    const Vector<int> &v =
            mutable_vector;

    int sum = 0;

    for (const int value: v) {
        sum += value;
    }

    return sum == 60;
}


bool test_ranges_interface() {
    Vector<int> v;

    v.push_back(40);
    v.push_back(10);
    v.push_back(30);
    v.push_back(20);

    if (std::ranges::size(v) != 4) {
        return false;
    }

    if (std::ranges::data(v) != v.data()) {
        return false;
    }

    std::ranges::sort(v);

    return v[0] == 10
           && v[1] == 20
           && v[2] == 30
           && v[3] == 40;
}


bool test_pop_back() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");

    const std::size_t old_capacity =
            v.capacity();

    v.pop_back();

    if (v.size() != 2) {
        return false;
    }

    if (v.capacity() != old_capacity) {
        return false;
    }

    if (v[0] != "zero"
        || v[1] != "one") {
        return false;
    }

    if (v.back() != "one") {
        return false;
    }

    return true;
}


bool test_pop_back_destroys_element() {
    PopBackProbe::live_count = 0;
    PopBackProbe::destructor_calls = 0;

    {
        Vector<PopBackProbe> v;

        /*
         * Prevent growth during the test so relocation
         * doesn't introduce additional destructor calls.
         */
        v.reserve(3);

        v.emplace_back(10);
        v.emplace_back(20);
        v.emplace_back(30);

        if (PopBackProbe::live_count != 3) {
            return false;
        }

        if (PopBackProbe::destructor_calls != 0) {
            return false;
        }

        v.pop_back();

        if (v.size() != 2) {
            return false;
        }

        if (PopBackProbe::live_count != 2) {
            return false;
        }

        if (PopBackProbe::destructor_calls != 1) {
            return false;
        }

        if (v.back().value != 20) {
            return false;
        }
    }

    /*
     * The remaining two objects must be destroyed
     * by Vector's destructor.
     */
    if (PopBackProbe::live_count != 0) {
        return false;
    }

    if (PopBackProbe::destructor_calls != 3) {
        return false;
    }

    return true;
}


bool test_resize_shrink() {
    Vector<std::string> v;

    v.reserve(8);

    v.push_back("zero");
    v.push_back("one");
    v.push_back("two");
    v.push_back("three");

    const std::size_t old_capacity =
            v.capacity();

    v.resize(2);

    if (v.size() != 2) {
        return false;
    }

    if (v.capacity() != old_capacity) {
        return false;
    }

    if (v[0] != "zero"
        || v[1] != "one") {
        return false;
    }

    return true;
}


bool test_resize_shrink_destroys_elements() {
    ResizeProbe::live_count = 0;
    ResizeProbe::destructor_calls = 0;

    {
        Vector<ResizeProbe> v;

        v.reserve(4);

        v.emplace_back(10);
        v.emplace_back(20);
        v.emplace_back(30);
        v.emplace_back(40);

        if (ResizeProbe::live_count != 4) {
            return false;
        }

        if (ResizeProbe::destructor_calls != 0) {
            return false;
        }

        v.resize(2);

        if (v.size() != 2) {
            return false;
        }

        if (ResizeProbe::live_count != 2) {
            return false;
        }

        if (ResizeProbe::destructor_calls != 2) {
            return false;
        }

        if (v[0].value != 10
            || v[1].value != 20) {
            return false;
        }
    }

    if (ResizeProbe::live_count != 0) {
        return false;
    }

    if (ResizeProbe::destructor_calls != 4) {
        return false;
    }

    return true;
}


bool test_resize_grow_within_capacity() {
    Vector<int> v;

    v.reserve(8);

    v.push_back(10);
    v.push_back(20);

    int *const old_data = v.data();
    const std::size_t old_capacity =
            v.capacity();

    v.resize(5);

    if (v.size() != 5) {
        return false;
    }

    if (v.capacity() != old_capacity) {
        return false;
    }

    /*
     * Growth fits in existing storage,
     * so no reallocation should occur.
     */
    if (v.data() != old_data) {
        return false;
    }

    if (v[0] != 10
        || v[1] != 20) {
        return false;
    }

    /*
     * resize(count) with no value argument
     * creates new value-initialized ints.
     */
    if (v[2] != 0
        || v[3] != 0
        || v[4] != 0) {
        return false;
    }

    return true;
}


bool test_resize_grow_within_capacity_exception_safety() {
    ThrowOnDefault::live_count = 0;
    ThrowOnDefault::destructor_calls = 0;
    ThrowOnDefault::defaults_before_throw = -1;

    {
        Vector<ThrowOnDefault> v;

        v.reserve(5);

        v.emplace_back(10);
        v.emplace_back(20);

        if (v.size() != 2) {
            return false;
        }

        if (ThrowOnDefault::live_count != 2) {
            return false;
        }

        const std::size_t old_capacity =
                v.capacity();

        ThrowOnDefault *const old_data =
                v.data();

        /*
         * resize(5) needs three default constructions:
         *
         * index 2 -> succeeds
         * index 3 -> throws
         */
        ThrowOnDefault::defaults_before_throw = 1;

        bool exception_caught = false;

        try {
            v.resize(5);
        } catch (const std::runtime_error &) {
            exception_caught = true;
        }

        if (!exception_caught) {
            return false;
        }

        /*
         * Strong guarantee:
         * the Vector remains logically unchanged.
         */
        if (v.size() != 2) {
            return false;
        }

        if (v.capacity() != old_capacity) {
            return false;
        }

        if (v.data() != old_data) {
            return false;
        }

        if (v[0].value != 10
            || v[1].value != 20) {
            return false;
        }

        /*
         * Only the two original objects remain alive.
         */
        if (ThrowOnDefault::live_count != 2) {
            return false;
        }

        /*
         * The one successfully constructed new object
         * must have been destroyed during rollback.
         */
        if (ThrowOnDefault::destructor_calls != 1) {
            return false;
        }
    }

    if (ThrowOnDefault::live_count != 0) {
        return false;
    }

    /*
     * 1 rollback destruction
     * +
     * 2 original elements destroyed by Vector
     */
    if (ThrowOnDefault::destructor_calls != 3) {
        return false;
    }

    return true;
}


bool test_resize_grow_with_reallocation() {
    Vector<int> v;

    v.push_back(10);
    v.push_back(20);

    int *const old_data = v.data();

    v.resize(10);

    if (v.size() != 10) {
        return false;
    }

    if (v.capacity() < 10) {
        return false;
    }

    /*
     * Reallocation must have occurred.
     */
    if (v.data() == old_data) {
        return false;
    }

    if (v[0] != 10
        || v[1] != 20) {
        return false;
    }

    for (std::size_t i = 2; i < 10; ++i) {
        if (v[i] != 0) {
            return false;
        }
    }

    return true;
}


bool test_resize_reallocation_default_exception_safety() {
    ThrowOnDefault::live_count = 0;
    ThrowOnDefault::destructor_calls = 0;
    ThrowOnDefault::defaults_before_throw = -1;

    {
        Vector<ThrowOnDefault> v;

        /*
         * Exactly two slots, so resize(5)
         * must allocate a new block.
         */
        v.reserve(2);

        v.emplace_back(10);
        v.emplace_back(20);

        ThrowOnDefault *const old_data =
                v.data();

        const std::size_t old_capacity =
                v.capacity();

        /*
         * resize(5) needs three default-constructed
         * tail elements.
         *
         * index 2 -> succeeds
         * index 3 -> throws
         */
        ThrowOnDefault::defaults_before_throw = 1;

        bool exception_caught = false;

        try {
            v.resize(5);
        } catch (const std::runtime_error &) {
            exception_caught = true;
        }

        if (!exception_caught) {
            return false;
        }

        /*
         * Original Vector must be completely unchanged.
         */
        if (v.size() != 2) {
            return false;
        }

        if (v.capacity() != old_capacity) {
            return false;
        }

        if (v.data() != old_data) {
            return false;
        }

        if (v[0].value != 10
            || v[1].value != 20) {
            return false;
        }

        /*
         * Only the two original objects remain alive.
         */
        if (ThrowOnDefault::live_count != 2) {
            return false;
        }

        /*
         * One new tail object was successfully
         * constructed and then rolled back.
         */
        if (ThrowOnDefault::destructor_calls != 1) {
            return false;
        }
    }

    if (ThrowOnDefault::live_count != 0) {
        return false;
    }

    /*
     * 1 rollback destruction
     * +
     * 2 original elements at Vector destruction.
     */
    if (ThrowOnDefault::destructor_calls != 3) {
        return false;
    }

    return true;
}


bool test_resize_reallocation_relocation_exception_safety() {
    ResizeRelocationProbe::live_count = 0;
    ResizeRelocationProbe::destructor_calls = 0;
    ResizeRelocationProbe::copies = 0;
    ResizeRelocationProbe::moves = 0;
    ResizeRelocationProbe::copies_before_throw = -1;

    {
        Vector<ResizeRelocationProbe> v;

        v.reserve(2);

        v.emplace_back(10);
        v.emplace_back(20);

        ResizeRelocationProbe *const old_data =
                v.data();

        const std::size_t old_capacity =
                v.capacity();

        /*
         * resize(5):
         *
         * 1. default-construct indices 2, 3, 4
         * 2. relocate index 0 -> copy succeeds
         * 3. relocate index 1 -> copy throws
         */
        ResizeRelocationProbe::copies_before_throw = 1;

        bool exception_caught = false;

        try {
            v.resize(5);
        } catch (const std::runtime_error &) {
            exception_caught = true;
        }

        if (!exception_caught) {
            return false;
        }

        /*
         * Strong guarantee:
         * original container is unchanged.
         */
        if (v.size() != 2) {
            return false;
        }

        if (v.capacity() != old_capacity) {
            return false;
        }

        if (v.data() != old_data) {
            return false;
        }

        if (v[0].value != 10
            || v[1].value != 20) {
            return false;
        }

        /*
         * move_if_noexcept should have selected copy.
         */
        if (ResizeRelocationProbe::copies != 1) {
            return false;
        }

        if (ResizeRelocationProbe::moves != 0) {
            return false;
        }

        /*
         * Only the two original objects should
         * remain alive after rollback.
         */
        if (ResizeRelocationProbe::live_count != 2) {
            return false;
        }

        /*
         * Rollback destroys:
         *
         * 1 successfully copied old element
         * +
         * 3 default-constructed tail elements
         */
        if (ResizeRelocationProbe::destructor_calls != 4) {
            return false;
        }
    }

    if (ResizeRelocationProbe::live_count != 0) {
        return false;
    }

    /*
     * Four rollback destructions
     * +
     * two original elements when v dies.
     */
    if (ResizeRelocationProbe::destructor_calls != 6) {
        return false;
    }

    return true;
}


bool test_resize_fill_grow_within_capacity() {
    Vector<int> v;

    v.reserve(8);

    v.push_back(10);
    v.push_back(20);

    int *const old_data = v.data();
    const std::size_t old_capacity =
            v.capacity();

    v.resize(5, 99);

    if (v.size() != 5) {
        return false;
    }

    if (v.capacity() != old_capacity) {
        return false;
    }

    if (v.data() != old_data) {
        return false;
    }

    if (v[0] != 10
        || v[1] != 20) {
        return false;
    }

    if (v[2] != 99
        || v[3] != 99
        || v[4] != 99) {
        return false;
    }

    return true;
}


bool test_resize_fill_alias_within_capacity() {
    Vector<std::string> v;

    v.reserve(8);

    v.push_back("zero");
    v.push_back("one");

    v.resize(5, v[0]);

    if (v.size() != 5) {
        return false;
    }

    return v[0] == "zero"
           && v[1] == "one"
           && v[2] == "zero"
           && v[3] == "zero"
           && v[4] == "zero";
}


bool test_resize_fill_grow_with_reallocation() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");

    std::string *const old_data = v.data();

    v.resize(10, std::string{"fill"});

    if (v.size() != 10) {
        return false;
    }

    if (v.capacity() < 10) {
        return false;
    }

    if (v.data() == old_data) {
        return false;
    }

    if (v[0] != "zero"
        || v[1] != "one") {
        return false;
    }

    for (std::size_t i = 2; i < 10; ++i) {
        if (v[i] != "fill") {
            return false;
        }
    }

    return true;
}


bool test_resize_fill_alias_with_reallocation() {
    Vector<std::string> v;

    v.push_back("zero");
    v.push_back("one");

    std::string *const old_data = v.data();

    /*
     * value aliases an element in the allocation
     * that resize() is about to replace.
     */
    v.resize(10, v[0]);

    if (v.size() != 10) {
        return false;
    }

    if (v.capacity() < 10) {
        return false;
    }

    if (v.data() == old_data) {
        return false;
    }

    if (v[0] != "zero"
        || v[1] != "one") {
        return false;
    }

    for (std::size_t i = 2; i < 10; ++i) {
        if (v[i] != "zero") {
            return false;
        }
    }

    return true;
}


bool test_resize_fill_reallocation_exception_safety() {
    ResizeFillProbe::live_count = 0;
    ResizeFillProbe::destructor_calls = 0;
    ResizeFillProbe::copies = 0;
    ResizeFillProbe::copies_before_throw = -1;

    {
        Vector<ResizeFillProbe> v;

        v.reserve(2);

        v.emplace_back(10);
        v.emplace_back(20);

        ResizeFillProbe fill{99};

        ResizeFillProbe *const old_data =
                v.data();

        const std::size_t old_capacity =
                v.capacity();

        /*
         * resize(5, fill) needs three fill copies.
         *
         * copy #1 succeeds
         * copy #2 throws
         */
        ResizeFillProbe::copies_before_throw = 1;

        bool exception_caught = false;

        try {
            v.resize(5, fill);
        } catch (const std::runtime_error &) {
            exception_caught = true;
        }

        if (!exception_caught) {
            return false;
        }

        /*
         * Original Vector must remain unchanged.
         */
        if (v.size() != 2) {
            return false;
        }

        if (v.capacity() != old_capacity) {
            return false;
        }

        if (v.data() != old_data) {
            return false;
        }

        if (v[0].value != 10
            || v[1].value != 20) {
            return false;
        }

        /*
         * Two original elements + external fill object.
         */
        if (ResizeFillProbe::live_count != 3) {
            return false;
        }

        /*
         * Exactly one successful new fill object
         * must have been destroyed during rollback.
         */
        if (ResizeFillProbe::destructor_calls != 1) {
            return false;
        }
    }

    /*
     * v's two elements + fill must all be destroyed.
     */
    if (ResizeFillProbe::live_count != 0) {
        return false;
    }

    /*
     * 1 rollback destruction
     * +
     * 3 scope-exit destructions
     */
    if (ResizeFillProbe::destructor_calls != 4) {
        return false;
    }

    return true;
}


bool test_resize_fill_relocation_exception_safety() {
    ResizeRelocationProbe::live_count = 0;
    ResizeRelocationProbe::destructor_calls = 0;
    ResizeRelocationProbe::copies = 0;
    ResizeRelocationProbe::moves = 0;
    ResizeRelocationProbe::copies_before_throw = -1;

    {
        Vector<ResizeRelocationProbe> v;

        v.reserve(2);

        v.emplace_back(10);
        v.emplace_back(20);

        ResizeRelocationProbe fill{99};

        ResizeRelocationProbe *const old_data =
                v.data();

        const std::size_t old_capacity =
                v.capacity();

        /*
         * resize(5, fill):
         *
         * 3 fill copies succeed
         * relocation copy of old[0] succeeds
         * relocation copy of old[1] throws
         */
        ResizeRelocationProbe::copies_before_throw = 4;

        bool exception_caught = false;

        try {
            v.resize(5, fill);
        } catch (const std::runtime_error &) {
            exception_caught = true;
        }

        if (!exception_caught) {
            return false;
        }

        if (v.size() != 2) {
            return false;
        }

        if (v.capacity() != old_capacity) {
            return false;
        }

        if (v.data() != old_data) {
            return false;
        }

        if (v[0].value != 10
            || v[1].value != 20) {
            return false;
        }

        /*
         * 3 fill copies
         * +
         * 1 successful relocation copy
         */
        if (ResizeRelocationProbe::copies != 4) {
            return false;
        }

        if (ResizeRelocationProbe::moves != 0) {
            return false;
        }

        /*
         * Original elements + external fill object.
         */
        if (ResizeRelocationProbe::live_count != 3) {
            return false;
        }

        /*
         * Rollback destroys:
         *
         * 1 relocated element
         * +
         * 3 fill elements
         */
        if (ResizeRelocationProbe::destructor_calls != 4) {
            return false;
        }
    }

    if (ResizeRelocationProbe::live_count != 0) {
        return false;
    }

    /*
     * 4 rollback destructions
     * +
     * 2 original vector elements
     * +
     * external fill object
     */
    if (ResizeRelocationProbe::destructor_calls != 7) {
        return false;
    }

    return true;
}


int main() {
    std::cout
            << "copy constructor: "
            << (test_copy_constructor()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "copy constructor exception safety: "
            << (test_copy_constructor_exception_safety()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "copy assignment: "
            << (test_copy_assignment()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "self-copy assignment: "
            << (test_self_copy_assignment()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "copy assignment exception safety: "
            << (test_copy_assignment_exception_safety()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "move constructor: "
            << (test_move_constructor()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "move assignment: "
            << (test_move_assignment()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "self-move assignment: "
            << (test_self_move_assignment()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "emplace_back: "
            << (test_emplace_back()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "push_back self-copy with reallocation: "
            << (test_push_back_self_copy_with_reallocation()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "push_back self-move with reallocation: "
            << (test_push_back_self_move_with_reallocation()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "at valid mutable access: "
            << (test_at_valid_mutable_access()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "at out of range: "
            << (test_at_out_of_range()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "at empty vector: "
            << (test_at_empty_vector()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "at const access: "
            << (test_at_const_access()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "front mutable: "
            << (test_front_mutable()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "front const: "
            << (test_front_const()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "back mutable: "
            << (test_back_mutable()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "back const: "
            << (test_back_const()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "data mutable: "
            << (test_data_mutable()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "data const: "
            << (test_data_const()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "begin/end mutable: "
            << (test_begin_end_mutable()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "begin/end empty: "
            << (test_begin_end_empty()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "begin/end const: "
            << (test_begin_end_const()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "range-based for: "
            << (test_range_based_for()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "std::sort: "
            << (test_std_sort()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "const range-based for: "
            << (test_const_range_based_for()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "ranges interface: "
            << (test_ranges_interface()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "pop_back: "
            << (test_pop_back()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "pop_back destroys element: "
            << (test_pop_back_destroys_element()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize shrink: "
            << (test_resize_shrink()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize shrink destroys elements: "
            << (test_resize_shrink_destroys_elements()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize grow within capacity: "
            << (test_resize_grow_within_capacity()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize grow within capacity exception safety: "
            << (test_resize_grow_within_capacity_exception_safety()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize grow with reallocation: "
            << (test_resize_grow_with_reallocation()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize reallocation default exception safety: "
            << (test_resize_reallocation_default_exception_safety()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize reallocation relocation exception safety: "
            << (test_resize_reallocation_relocation_exception_safety()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize fill grow within capacity: "
            << (test_resize_fill_grow_within_capacity()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize fill alias within capacity: "
            << (test_resize_fill_alias_within_capacity()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize fill grow with reallocation: "
            << (test_resize_fill_grow_with_reallocation()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize fill alias with reallocation: "
            << (test_resize_fill_alias_with_reallocation()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize fill reallocation exception safety: "
            << (test_resize_fill_reallocation_exception_safety()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    std::cout
            << "resize fill relocation exception safety: "
            << (test_resize_fill_relocation_exception_safety()
                    ? "PASS"
                    : "FAIL")
            << '\n';

    return 0;
}
