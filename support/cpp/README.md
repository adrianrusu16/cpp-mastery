# `cpp_mastery::test`

A small dependency-free C++ test framework built for the labs in this repository.
It is intentionally modest: the goal is to make tests readable and to expose the
mechanics of test execution rather than compete with Catch2 or GoogleTest.

## Architecture

```text
TestCase       one named `void()` test
TestSuite      a named collection of test cases
Assertions     CHECK / REQUIRE / equality / exception assertions
TestResult     failures, unexpected exceptions, and timing
ConsoleReporter
Runner         filtering, delegation, suite execution, and exit codes
```

`std::source_location` records the call site of failed assertions automatically.
`CHECK` records a failure and continues the test; `REQUIRE` records the failure
and aborts only the current test.

## Example

```cpp
#include <cpp_mastery/testing/Testing.hpp>

void grows()
{
    Vector<int> values;
    values.resize(3);

    REQUIRE_EQ(values.size(), 3U);
    CHECK_EQ(values[0], 0);
}

cpp_mastery::test::TestSuite make_resize_suite()
{
    cpp_mastery::test::TestSuite suite{"resize"};
    suite.add("grows", grows);
    return suite;
}
```

A runner composes suites explicitly:

```cpp
int main(int argc, char** argv)
{
    cpp_mastery::test::Runner runner{"Vector tests"};
    runner.add(make_resize_suite());
    return runner.run(argc, argv);
}
```

## CLI

```text
--list
--suite <substring>
--test <substring>
--verbose
--help
```

The explicit suite factories are deliberate. The framework avoids hidden static
registration so the control flow remains visible while learning C++.
