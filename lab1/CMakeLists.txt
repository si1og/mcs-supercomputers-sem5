cmake_minimum_required(VERSION 3.16)
project(bfloat16_converter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(bfloat16_converter
    src/main.cpp
    src/cli.cpp
    src/bfloat16.cpp
)

target_include_directories(bfloat16_converter PRIVATE include)

target_compile_options(bfloat16_converter PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
