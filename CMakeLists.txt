cmake_minimum_required(VERSION 3.31)

project(cpp_mastery
        VERSION 0.1
        LANGUAGES C CXX)

# C
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# C++
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cpp_mastery
        main.cpp
)

add_executable(vvector_lab
        c/vvector/main.c
        c/vvector/vvector.c
)

target_include_directories(vvector_lab PRIVATE
        c/vvector
)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vvector_lab PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow

            -fsanitize=address
            -fsanitize=undefined
            -fno-omit-frame-pointer
    )

    target_link_options(vvector_lab PRIVATE
            -fsanitize=address
            -fsanitize=undefined
    )
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cpp_mastery PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
endif()

add_executable(lifetime_lab
        cpp/lifetime/main.cpp
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lifetime_lab PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
endif()

add_executable(exams
        cpp/exams/main.cpp
        cpp/exams/expression_tree/ExpressionTree.cpp
        cpp/exams/expression_tree/bTree.cpp
        cpp/exams/expression_tree/bTree.h
        cpp/exams/sofa/sofa.cpp
        cpp/exams/sofa/sofa.h
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(exams PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
endif()

add_executable(raii_lab
        cpp/raii/main.cpp
        cpp/raii/IntBuffer.cpp
        cpp/raii/IntBuffer.h
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(raii_lab PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow

            -fsanitize=address
            -fsanitize=undefined
            -fno-omit-frame-pointer
    )

    target_link_options(raii_lab PRIVATE
            -fsanitize=address
            -fsanitize=undefined
    )
endif()

message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C compiler ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ compiler ID: ${CMAKE_CXX_COMPILER_ID}")