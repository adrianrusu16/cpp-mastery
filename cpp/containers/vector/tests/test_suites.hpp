#pragma once

#include <cpp_mastery/testing/TestSuite.hpp>

namespace cpp_mastery::vector_tests {

test::TestSuite make_copy_suite();
test::TestSuite make_move_suite();
test::TestSuite make_modifier_suite();
test::TestSuite make_access_suite();
test::TestSuite make_iteration_suite();
test::TestSuite make_resize_suite();
test::TestSuite make_resize_exception_suite();
test::TestSuite make_resize_fill_suite();
test::TestSuite make_resize_fill_exception_suite();

} // namespace cpp_mastery::vector_tests
