add_library(vvector STATIC
    src/vvector.c
)

target_include_directories(vvector PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
cpp_mastery_configure_target(vvector)

add_executable(vvector_tests
    tests/main.c
    tests/basic_tests.c
    tests/aliasing_tests.c
)

target_include_directories(vvector_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CPP_MASTERY_SUPPORT_DIR}/c
)

target_link_libraries(vvector_tests PRIVATE vvector)
cpp_mastery_configure_target(vvector_tests)
cpp_mastery_add_test(vvector vvector_tests)
