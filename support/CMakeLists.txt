add_library(cpp_mastery_testing INTERFACE)
target_include_directories(cpp_mastery_testing INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include
)
target_compile_features(cpp_mastery_testing INTERFACE cxx_std_26)
add_library(cpp_mastery::testing ALIAS cpp_mastery_testing)
