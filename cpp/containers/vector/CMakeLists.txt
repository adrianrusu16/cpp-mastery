add_library(cpp_mastery_vector INTERFACE)
target_include_directories(cpp_mastery_vector INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(cpp_mastery_vector INTERFACE cxx_std_26)

add_executable(containers_vector_lab
    examples/vector_demo.cpp
)
target_link_libraries(containers_vector_lab PRIVATE cpp_mastery_vector)
cpp_mastery_configure_target(containers_vector_lab)

add_executable(containers_vector_tests
    tests/main.cpp
    tests/copy_tests.cpp
    tests/move_tests.cpp
    tests/modifier_tests.cpp
    tests/access_tests.cpp
    tests/iteration_tests.cpp
    tests/resize_basic_tests.cpp
    tests/resize_exception_tests.cpp
    tests/resize_fill_tests.cpp
    tests/resize_fill_exception_tests.cpp
)
target_include_directories(containers_vector_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(containers_vector_tests PRIVATE
    cpp_mastery_vector
    cpp_mastery::testing
)
cpp_mastery_configure_target(containers_vector_tests)
cpp_mastery_add_suite_tests(vector containers_vector_tests
    copy
    move
    modifiers
    access
    iteration
    resize
    resize-exceptions
    resize-fill
    resize-fill-exceptions
)
