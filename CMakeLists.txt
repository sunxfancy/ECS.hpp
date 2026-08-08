cmake_minimum_required(VERSION 3.15)
project(ECS_hpp)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# zeroerr's TEST_CASE macros place _Pragma() between a function declarator and
# its body. That form is accepted by Clang/MSVC but rejected by GCC, so prefer
# Clang or MSVC when configuring the suite locally.
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/test
)

set(TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/test/table_world.cpp
    ${CMAKE_SOURCE_DIR}/test/defer_commit.cpp
    ${CMAKE_SOURCE_DIR}/test/handles.cpp
    ${CMAKE_SOURCE_DIR}/test/view_systems.cpp
    ${CMAKE_SOURCE_DIR}/test/visualize.cpp
    ${CMAKE_SOURCE_DIR}/test/main.cpp
)

add_executable(ecs_test ${TEST_SOURCES})

enable_testing()
add_test(NAME ecs_test COMMAND ecs_test)

