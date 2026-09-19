add_library(engine_renderer STATIC
    src/camera.cpp
    src/deferred_renderer.cpp
)

target_include_directories(engine_renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine_renderer
    PUBLIC
        engine::core
        # PUBLIC : l'API du renderer manipule des types rhi (maillages, textures, device).
        engine::rhi
    PRIVATE
        # Details d'implementation : le renderer charge ses propres shaders, mais ceux qui
        # l'utilisent n'ont pas a connaitre le systeme de fichiers.
        engine::assets
        engine::platform
)

set_project_warnings(engine_renderer)

add_library(engine::renderer ALIAS engine_renderer)
