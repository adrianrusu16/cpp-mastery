#include "test_suites.hpp"

#include <cpp_mastery/testing/Testing.hpp>

int main(const int argc, char** argv)
{
    cpp_mastery::test::Runner runner{"IntBuffer test suite"};
    runner.add(cpp_mastery::raii_tests::make_int_buffer_suite());
    return runner.run(argc, argv);
}
