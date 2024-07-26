cmake_minimum_required(VERSION 3.15)
project(FlexDesign)

set(CMAKE_CXX_STANDARD 17)
include_directories(${CMAKE_SOURCE_DIR}/include)
add_executable(FlexDesign ${CMAKE_SOURCE_DIR}/src/main.cpp) 

