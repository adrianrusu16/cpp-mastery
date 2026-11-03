add_library(cpp_mastery_testing INTERFACE)
target_include_directories(cpp_mastery_testing INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include
)
target_compile_features(cpp_mastery_testing INTERFACE cxx_std_26)
add_library(cpp_mastery::testing ALIAS cpp_mastery_testing)

if(BUILD_TESTING)
    add_executable(cpp_mastery_testing_self_tests
        cpp/tests/main.cpp
        cpp/tests/assertion_tests.cpp
    )
    target_link_libraries(cpp_mastery_testing_self_tests PRIVATE cpp_mastery::testing)
    cpp_mastery_configure_target(cpp_mastery_testing_self_tests)
    cpp_mastery_add_test(testing.assertions cpp_mastery_testing_self_tests)
endif()
