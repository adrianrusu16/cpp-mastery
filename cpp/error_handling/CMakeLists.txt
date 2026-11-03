add_library(cpp_mastery_parsing STATIC
    src/ParsePositiveInt.cpp
    src/ParseZeroBasedIndex.cpp
)
target_include_directories(cpp_mastery_parsing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(cpp_mastery_parsing PUBLIC cxx_std_23)
cpp_mastery_configure_target(cpp_mastery_parsing)
add_library(cpp_mastery::parsing ALIAS cpp_mastery_parsing)

if(BUILD_TESTING)
    add_executable(cpp_mastery_parsing_tests
        tests/main.cpp
        tests/parse_positive_int_tests.cpp
        tests/parse_zero_based_index_tests.cpp
    )
    target_link_libraries(cpp_mastery_parsing_tests PRIVATE
        cpp_mastery::parsing
        cpp_mastery::testing
    )
    cpp_mastery_configure_target(cpp_mastery_parsing_tests)
    cpp_mastery_add_suite_tests(parsing cpp_mastery_parsing_tests positive-int zero-based-index)
endif()
