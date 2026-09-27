add_library(int_buffer STATIC
    src/IntBuffer.cpp
)
target_include_directories(int_buffer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
cpp_mastery_configure_target(int_buffer)

add_executable(raii_lab
    tests/main.cpp
    tests/int_buffer_tests.cpp
)
target_include_directories(raii_lab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(raii_lab PRIVATE
    int_buffer
    cpp_mastery::testing
)
cpp_mastery_configure_target(raii_lab)
cpp_mastery_add_suite_tests(raii raii_lab int-buffer)
