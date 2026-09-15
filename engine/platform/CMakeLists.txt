find_package(SDL3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_library(engine_platform STATIC
    src/window.cpp
    src/input.cpp
    src/application.cpp
)

target_include_directories(engine_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine_platform
    PUBLIC
        engine::core
    PRIVATE
        SDL3::SDL3
        OpenGL::GL
)

set_project_warnings(engine_platform)

add_library(engine::platform ALIAS engine_platform)
