#include "bootstrap.hpp"

#include <iostream>

void run_assertion_tests(bootstrap::Checks& checks);
void run_assertion_semantics_tests(bootstrap::Checks& checks);
void run_runner_tests(bootstrap::Checks& checks);

int main()
{
    bootstrap::Checks checks;
    run_assertion_tests(checks);
    run_assertion_semantics_tests(checks);
    run_runner_tests(checks);

    std::cout << "\n"
              << checks.assertions() << " bootstrap assertions: " << checks.failures()
              << " failed\n";
    return checks.failures() == 0U ? 0 : 1;
}
