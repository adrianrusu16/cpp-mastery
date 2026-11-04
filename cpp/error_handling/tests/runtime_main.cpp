#include <cpp_mastery/testing/Runner.hpp>

cpp_mastery::test::TestSuite make_runtime_transaction_suite();
cpp_mastery::test::TestSuite make_runtime_worker_pool_suite();
cpp_mastery::test::TestSuite make_runtime_snapshot_publication_suite();
cpp_mastery::test::TestSuite make_runtime_gpu_retirement_suite();
cpp_mastery::test::TestSuite make_runtime_multi_queue_retirement_suite();
cpp_mastery::test::TestSuite make_runtime_cross_queue_handoff_suite();
cpp_mastery::test::TestSuite make_runtime_coordinated_pipeline_suite();

int main(int argc, char** argv)
{
    cpp_mastery::test::Runner runner{"Transactional runtime settings"};
    runner.add(make_runtime_transaction_suite());
    runner.add(make_runtime_worker_pool_suite());
    runner.add(make_runtime_snapshot_publication_suite());
    runner.add(make_runtime_gpu_retirement_suite());
    runner.add(make_runtime_multi_queue_retirement_suite());
    runner.add(make_runtime_cross_queue_handoff_suite());
    runner.add(make_runtime_coordinated_pipeline_suite());
    return runner.run(argc, argv);
}
