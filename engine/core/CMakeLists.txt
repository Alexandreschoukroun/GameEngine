add_library(engine_core STATIC
    src/log.cpp
    src/time.cpp
)

target_include_directories(engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(engine_core PUBLIC cxx_std_20)
set_project_warnings(engine_core)

add_library(engine::core ALIAS engine_core)
