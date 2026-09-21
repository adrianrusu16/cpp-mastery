#include "Vector.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <stdexcept>


struct alignas(64) CacheLineData {
    int values[16]{};
};


struct SmallData {
    int value{};
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

    return 0;
}
