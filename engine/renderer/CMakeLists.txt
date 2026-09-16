add_library(engine_renderer STATIC
    src/camera.cpp
)

target_include_directories(engine_renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# La camera ne produit que des matrices : elle n'a rien a demander au GPU, donc aucun
# lien vers rhi. La couche se remplira en M2 avec les passes et les materiaux.
target_link_libraries(engine_renderer PUBLIC engine::core)

set_project_warnings(engine_renderer)

add_library(engine::renderer ALIAS engine_renderer)
