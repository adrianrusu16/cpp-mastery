#include <cstddef>
#include <iostream>
#include <utility>

#include "IntBuffer.h"


using test_function = bool (*)();


static bool test_construction()
{
    IntBuffer buffer{3};

    if (buffer.size() != 3) {
        return false;
    }

    buffer[0] = 10;
    buffer[1] = 20;
    buffer[2] = 30;

    return buffer[0] == 10
        && buffer[1] == 20
        && buffer[2] == 30;
}


static bool test_copy_construction()
{
    IntBuffer original{3};

    original[0] = 10;
    original[1] = 20;
    original[2] = 30;

    IntBuffer copy{original};

    if (copy.size() != original.size()) {
        return false;
    }

    if (copy[0] != 10
        || copy[1] != 20
        || copy[2] != 30) {
        return false;
    }

    /*
     * This is the important part:
     * prove that the copy owns independent storage.
     */
    copy[0] = 999;

    return original[0] == 10
        && copy[0] == 999;
}


static bool test_copy_assignment()
{
    IntBuffer source{3};

    source[0] = 10;
    source[1] = 20;
    source[2] = 30;

    IntBuffer destination{5};

    destination[0] = 100;
    destination[1] = 200;

    destination = source;

    if (destination.size() != 3) {
        return false;
    }

    if (destination[0] != 10
        || destination[1] != 20
        || destination[2] != 30) {
        return false;
    }

    destination[1] = 999;

    return source[1] == 20
        && destination[1] == 999;
}


static bool test_move_construction()
{
    IntBuffer source{3};

    source[0] = 10;
    source[1] = 20;
    source[2] = 30;

    IntBuffer destination{std::move(source)};

    if (destination.size() != 3) {
        return false;
    }

    if (destination[0] != 10
        || destination[1] != 20
        || destination[2] != 30) {
        return false;
    }

    /*
     * According to OUR implementation, moved-from
     * IntBuffer becomes empty.
     */
    return source.size() == 0;
}


static bool test_move_assignment()
{
    IntBuffer source{3};

    source[0] = 10;
    source[1] = 20;
    source[2] = 30;

    IntBuffer destination{5};

    destination[0] = 100;
    destination[1] = 200;

    destination = std::move(source);

    if (destination.size() != 3) {
        return false;
    }

    if (destination[0] != 10
        || destination[1] != 20
        || destination[2] != 30) {
        return false;
    }

    return source.size() == 0;
}


static bool test_self_copy_assignment()
{
    IntBuffer buffer{3};

    buffer[0] = 10;
    buffer[1] = 20;
    buffer[2] = 30;

    buffer = buffer;

    return buffer.size() == 3
        && buffer[0] == 10
        && buffer[1] == 20
        && buffer[2] == 30;
}


static bool test_self_move_assignment()
{
    IntBuffer buffer{3};

    buffer[0] = 10;
    buffer[1] = 20;
    buffer[2] = 30;

    buffer = std::move(buffer);

    /*
     * Do not assume the old value survived self-move.
     * Instead prove the object can still be assigned
     * a new valid state.
     */
    buffer = IntBuffer{2};

    buffer[0] = 42;
    buffer[1] = 84;

    return buffer.size() == 2
        && buffer[0] == 42
        && buffer[1] == 84;
}


static bool test_reassignment_after_move()
{
    IntBuffer source{3};

    source[0] = 10;
    source[1] = 20;
    source[2] = 30;

    IntBuffer destination{std::move(source)};

    /*
     * source is still a living object.
     * It should be assignable again.
     */
    IntBuffer replacement{2};

    replacement[0] = 42;
    replacement[1] = 84;

    source = replacement;

    return source.size() == 2
        && source[0] == 42
        && source[1] == 84
        && destination.size() == 3
        && destination[0] == 10;
}


static void run_test(
    const char* name,
    const test_function test,
    std::size_t& passed,
    std::size_t& failed)
{
    std::cout << name;

    if (test()) {
        std::cout << "PASS\n";
        ++passed;
    } else {
        std::cout << "FAIL\n";
        ++failed;
    }
}


int main()
{
    std::size_t passed = 0;
    std::size_t failed = 0;

    run_test(
        "construction                           ",
        test_construction,
        passed,
        failed
    );

    run_test(
        "copy construction                      ",
        test_copy_construction,
        passed,
        failed
    );

    run_test(
        "copy assignment                        ",
        test_copy_assignment,
        passed,
        failed
    );

    run_test(
        "move construction                      ",
        test_move_construction,
        passed,
        failed
    );

    run_test(
        "move assignment                        ",
        test_move_assignment,
        passed,
        failed
    );

    run_test(
        "self-copy assignment                   ",
        test_self_copy_assignment,
        passed,
        failed
    );

    run_test(
        "self-move assignment                   ",
        test_self_move_assignment,
        passed,
        failed
    );

    run_test(
        "reassignment after move                ",
        test_reassignment_after_move,
        passed,
        failed
    );

    std::cout << '\n';
    std::cout << "========================================\n";
    std::cout << "Passed: " << passed << '\n';
    std::cout << "Failed: " << failed << '\n';
    std::cout << "========================================\n";

    return failed == 0 ? 0 : 1;
}