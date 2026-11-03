#include <cpp_mastery/testing/Runner.hpp>

#include <utility>

cpp_mastery::test::TestSuite make_positive_int_suite();
cpp_mastery::test::TestSuite make_zero_based_index_suite();

int main(int argc, char** argv)
{
    cpp_mastery::test::Runner runner{"Positive integer parsing"};
    runner.add(make_positive_int_suite());
    runner.add(make_zero_based_index_suite());
    return runner.run(argc, argv);
}
