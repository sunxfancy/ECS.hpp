cmake_minimum_required(VERSION 3.15)
project(ECS_hpp)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS_DEBUG "-glldb -O0 -D_GLIBCXX_DEBUG")

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/test
)

set(TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/test/main.cpp
    ${CMAKE_SOURCE_DIR}/test/visualize.cpp
)

add_executable(ecs_test ${TEST_SOURCES}) 

