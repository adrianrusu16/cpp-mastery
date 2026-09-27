include_guard(GLOBAL)

option(CPP_MASTERY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CPP_MASTERY_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(cpp_mastery_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(CPP_MASTERY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
        if(CPP_MASTERY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(cpp_mastery_enable_sanitizers target)
    if(NOT CPP_MASTERY_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        message(STATUS "Sanitizers are skipped for ${target} on MSVC")
        return()
    endif()

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -fsanitize=address
            -fsanitize=undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address
            -fsanitize=undefined
        )
    endif()
endfunction()

function(cpp_mastery_configure_target target)
    cpp_mastery_apply_warnings(${target})
    cpp_mastery_enable_sanitizers(${target})
endfunction()

function(cpp_mastery_add_test name target)
    if(BUILD_TESTING)
        add_test(NAME ${name} COMMAND ${target})
        set_tests_properties(${name} PROPERTIES LABELS "${name}")
    endif()
endfunction()


function(cpp_mastery_add_suite_tests prefix target)
    if(NOT BUILD_TESTING)
        return()
    endif()

    foreach(suite IN LISTS ARGN)
        string(REGEX REPLACE "[^A-Za-z0-9_.-]" "-" test_suffix "${suite}")
        add_test(NAME "${prefix}.${test_suffix}" COMMAND ${target} --suite "${suite}")
        set_tests_properties("${prefix}.${test_suffix}" PROPERTIES LABELS "${prefix}")
    endforeach()
endfunction()
