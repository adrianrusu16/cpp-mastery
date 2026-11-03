#include <cpp_mastery/testing/Runner.hpp>

cpp_mastery::test::TestSuite make_runtime_transaction_suite();
cpp_mastery::test::TestSuite make_runtime_worker_pool_suite();

int main(int argc, char** argv)
{
    cpp_mastery::test::Runner runner{"Transactional runtime settings"};
    runner.add(make_runtime_transaction_suite());
    runner.add(make_runtime_worker_pool_suite());
    return runner.run(argc, argv);
}
