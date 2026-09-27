#include "test_suites.hpp"

#include <cpp_mastery/testing/Testing.hpp>
#include <cpp_mastery/vector/Vector.hpp>

#include <stdexcept>
#include <string>

namespace cpp_mastery::vector_tests {
namespace {

void at_returns_mutable_reference()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");

    std::string& element = values.at(1);
    CHECK_EQ(element, "one");

    element = "changed";
    CHECK_EQ(values[1], "changed");
}

void at_throws_out_of_range()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");

    CHECK_THROWS_AS(values.at(3), std::out_of_range);
}

void at_throws_on_empty_vector()
{
    Vector<int> values;
    CHECK_THROWS_AS(values.at(0), std::out_of_range);
}

void at_preserves_constness()
{
    Vector<std::string> mutable_values;
    mutable_values.push_back("zero");
    mutable_values.push_back("one");
    mutable_values.push_back("two");

    const Vector<std::string>& values = mutable_values;
    const std::string& element = values.at(1);

    CHECK_EQ(element, "one");
}

void front_returns_first_mutable_element()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");

    std::string& first = values.front();
    CHECK_EQ(first, "zero");
    CHECK_EQ(&first, &values[0]);

    first = "changed";
    CHECK_EQ(values[0], "changed");
}

void front_preserves_constness()
{
    Vector<std::string> mutable_values;
    mutable_values.push_back("zero");
    mutable_values.push_back("one");
    mutable_values.push_back("two");

    const Vector<std::string>& values = mutable_values;
    const std::string& first = values.front();

    CHECK_EQ(first, "zero");
    CHECK_EQ(&first, &values[0]);
}

void back_returns_last_mutable_element()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");

    std::string& last = values.back();
    CHECK_EQ(last, "two");
    CHECK_EQ(&last, &values[2]);

    last = "changed";
    CHECK_EQ(values[2], "changed");
}

void back_preserves_constness()
{
    Vector<std::string> mutable_values;
    mutable_values.push_back("zero");
    mutable_values.push_back("one");
    mutable_values.push_back("two");

    const Vector<std::string>& values = mutable_values;
    const std::string& last = values.back();

    CHECK_EQ(last, "two");
    CHECK_EQ(&last, &values[2]);
}

void data_exposes_mutable_contiguous_storage()
{
    Vector<std::string> values;
    values.push_back("zero");
    values.push_back("one");
    values.push_back("two");

    std::string* data = values.data();
    REQUIRE_EQ(data, &values[0]);
    CHECK_EQ(data[0], "zero");
    CHECK_EQ(data[1], "one");
    CHECK_EQ(data[2], "two");

    data[1] = "changed";
    CHECK_EQ(values[1], "changed");
}

void data_preserves_constness()
{
    Vector<std::string> mutable_values;
    mutable_values.push_back("zero");
    mutable_values.push_back("one");
    mutable_values.push_back("two");

    const Vector<std::string>& values = mutable_values;
    const std::string* data = values.data();

    REQUIRE_EQ(data, &values[0]);
    CHECK_EQ(data[0], "zero");
    CHECK_EQ(data[1], "one");
    CHECK_EQ(data[2], "two");
}

} // namespace

test::TestSuite make_access_suite()
{
    test::TestSuite suite{"access"};
    suite.add("at returns mutable reference", at_returns_mutable_reference)
        .add("at throws out of range", at_throws_out_of_range)
        .add("at throws on empty vector", at_throws_on_empty_vector)
        .add("at preserves constness", at_preserves_constness)
        .add("front returns first mutable element", front_returns_first_mutable_element)
        .add("front preserves constness", front_preserves_constness)
        .add("back returns last mutable element", back_returns_last_mutable_element)
        .add("back preserves constness", back_preserves_constness)
        .add("data exposes mutable contiguous storage", data_exposes_mutable_contiguous_storage)
        .add("data preserves constness", data_preserves_constness);
    return suite;
}

} // namespace cpp_mastery::vector_tests
