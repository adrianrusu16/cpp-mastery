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

# Higher-level domain layer: explicitly depends on the lower-level parser.
add_library(cpp_mastery_config STATIC
    src/ReadWorkerCount.cpp
    src/ReadRenderDistance.cpp
    src/ReadEngineConfig.cpp
)
target_include_directories(cpp_mastery_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cpp_mastery_config PUBLIC cpp_mastery::parsing)
target_compile_features(cpp_mastery_config PUBLIC cxx_std_23)
cpp_mastery_configure_target(cpp_mastery_config)
add_library(cpp_mastery::config ALIAS cpp_mastery_config)

if(BUILD_TESTING)
    add_executable(cpp_mastery_parsing_tests
        tests/main.cpp
        tests/parse_positive_int_tests.cpp
        tests/parse_zero_based_index_tests.cpp
        tests/read_worker_count_tests.cpp
        tests/read_render_distance_tests.cpp
        tests/read_engine_config_tests.cpp
    )
    target_link_libraries(cpp_mastery_parsing_tests PRIVATE
        cpp_mastery::config
        cpp_mastery::testing
    )
    cpp_mastery_configure_target(cpp_mastery_parsing_tests)
    cpp_mastery_add_suite_tests(parsing cpp_mastery_parsing_tests positive-int zero-based-index)
    cpp_mastery_add_suite_tests(config cpp_mastery_parsing_tests
        worker-count render-distance engine-config
    )
endif()

# Phase 2 / Lesson 5: resource preparation is fallible; committing is not.
add_library(cpp_mastery_runtime STATIC
    src/RuntimeState.cpp
    src/ParkedWorkerPool.cpp
    src/WorkerRuntime.cpp
)
target_include_directories(cpp_mastery_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cpp_mastery_runtime PUBLIC cpp_mastery::config)
target_compile_features(cpp_mastery_runtime PUBLIC cxx_std_23)
cpp_mastery_configure_target(cpp_mastery_runtime)
add_library(cpp_mastery::runtime ALIAS cpp_mastery_runtime)

if(BUILD_TESTING)
    add_executable(cpp_mastery_runtime_tests
        tests/runtime_main.cpp
        tests/runtime_transaction_tests.cpp
        tests/runtime_worker_transaction_tests.cpp
    )
    target_link_libraries(cpp_mastery_runtime_tests PRIVATE
        cpp_mastery::runtime
        cpp_mastery::testing
    )
    cpp_mastery_configure_target(cpp_mastery_runtime_tests)
    cpp_mastery_add_suite_tests(runtime cpp_mastery_runtime_tests transaction worker-pool)
endif()
