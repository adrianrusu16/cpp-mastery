#include <cpp_mastery/testing/Runner.hpp>

#include <utility>

cpp_mastery::test::TestSuite make_positive_int_suite();
cpp_mastery::test::TestSuite make_zero_based_index_suite();
cpp_mastery::test::TestSuite make_worker_count_suite();
cpp_mastery::test::TestSuite make_render_distance_suite();
cpp_mastery::test::TestSuite make_engine_config_suite();

int main(int argc, char** argv)
{
    cpp_mastery::test::Runner runner{"Parsing and configuration contracts"};
    runner.add(make_positive_int_suite());
    runner.add(make_zero_based_index_suite());
    runner.add(make_worker_count_suite());
    runner.add(make_render_distance_suite());
    runner.add(make_engine_config_suite());
    return runner.run(argc, argv);
}
