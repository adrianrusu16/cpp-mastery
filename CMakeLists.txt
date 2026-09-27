cmake_minimum_required(VERSION 3.31)

project(cpp_mastery
    VERSION 1.0.0
    DESCRIPTION "Hands-on C and modern C++ systems learning labs"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# No lab currently uses C++ modules; disabling dependency scanning keeps
# Clang toolchains usable even when clang-scan-deps is not installed.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(CTest)
include(cmake/ProjectOptions.cmake)

set(CPP_MASTERY_SUPPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/support")

add_subdirectory(support)
add_subdirectory(c)
add_subdirectory(cpp)

option(CPP_MASTERY_BUILD_ARCHIVE "Build archived academic exercises" OFF)
if(CPP_MASTERY_BUILD_ARCHIVE)
    add_subdirectory(archive/academic_exercises)
endif()

message(STATUS "C compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Warnings as errors: ${CPP_MASTERY_WARNINGS_AS_ERRORS}")
message(STATUS "Sanitizers: ${CPP_MASTERY_ENABLE_SANITIZERS}")
