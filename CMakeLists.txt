cmake_minimum_required(VERSION 3.25)
project(GameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)

option(GAMEENGINE_BUILD_TESTS "Build the doctest test suite" ON)

add_subdirectory(engine)
add_subdirectory(game)

if(GAMEENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
