#include <cpp_mastery/testing/Runner.hpp>

#include <utility>

cpp_mastery::test::TestSuite make_positive_int_suite();

int main(int argc, char** argv)
{
    cpp_mastery::test::Runner runner{"Positive integer parsing"};
    runner.add(make_positive_int_suite());
    return runner.run(argc, argv);
}
