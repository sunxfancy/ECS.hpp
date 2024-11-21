cmake_minimum_required(VERSION 3.15)
project(FlexDesign)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS_DEBUG "-glldb -O0 -D_GLIBCXX_DEBUG")

include_directories(${CMAKE_SOURCE_DIR}/include)
add_executable(FlexDesign ${CMAKE_SOURCE_DIR}/src/main.cpp ${CMAKE_SOURCE_DIR}/src/visualize.cpp) 

