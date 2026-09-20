cmake_minimum_required(VERSION 3.25)
project(GameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)

option(GAMEENGINE_BUILD_TESTS "Build the doctest test suite" ON)

# Active dans les deux configurations : un profil mesure sur un build Debug ne dit rien du
# budget de 16,6 ms, qui ne se verifie qu'en Release. A passer a OFF pour les builds
# distribues (M9) : le client Tracy ouvre un port reseau.
option(GAMEENGINE_PROFILING "Instrument the engine for the Tracy profiler" ON)

add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(tools)

if(GAMEENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
