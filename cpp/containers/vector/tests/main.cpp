#include "test_suites.hpp"

#include <cpp_mastery/testing/Testing.hpp>

int main(const int argc, char** argv)
{
    using cpp_mastery::test::Runner;
    using namespace cpp_mastery::vector_tests;

    Runner runner{"Vector<T> test suite"};
    runner.add(make_copy_suite())
        .add(make_move_suite())
        .add(make_modifier_suite())
        .add(make_access_suite())
        .add(make_iteration_suite())
        .add(make_resize_suite())
        .add(make_resize_exception_suite())
        .add(make_resize_fill_suite())
        .add(make_resize_fill_exception_suite());

    return runner.run(argc, argv);
}
