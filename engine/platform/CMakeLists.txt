find_package(SDL3 CONFIG REQUIRED)

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
        # platform cree le contexte OpenGL via SDL mais n'appelle plus aucune fonction GL :
        # c'est le role de rhi, qui est au-dessus. Plus de lien vers OpenGL ici.
        SDL3::SDL3
)

set_project_warnings(engine_platform)

add_library(engine::platform ALIAS engine_platform)
