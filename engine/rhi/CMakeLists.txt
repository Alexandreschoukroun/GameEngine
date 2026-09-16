find_package(glad CONFIG REQUIRED)

add_library(engine_rhi STATIC
    src/device.cpp
    src/mesh.cpp
    src/shader_program.cpp
    src/texture.cpp
)

target_include_directories(engine_rhi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine_rhi
    PUBLIC
        engine::core
    PRIVATE
        # PRIVATE : les en-tetes OpenGL ne se propagent pas. Aucune couche au-dessus de rhi
        # ne peut inclure glad, meme par accident. La regle d'architecture est verifiee
        # par le compilateur.
        glad::glad
)

set_project_warnings(engine_rhi)

add_library(engine::rhi ALIAS engine_rhi)
