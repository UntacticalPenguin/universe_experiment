cmake_minimum_required(VERSION 3.16)

project(universe_cpp_step1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(universe_cpp_step1
    src/main.cpp
    src/catalog.cpp
    src/simulation.cpp
    src/svg_writer.cpp
)

target_include_directories(universe_cpp_step1 PRIVATE src)