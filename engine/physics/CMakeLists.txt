find_package(Jolt CONFIG REQUIRED)

add_library(engine_physics STATIC
    src/world.cpp
)

target_include_directories(engine_physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine_physics
    PUBLIC
        engine::core
    PRIVATE
        # PRIVATE : aucun en-tete de Jolt ne remonte au-dessus de cette couche. L'API
        # publique ne parle que de Vec3, de Quat et d'identifiants entiers.
        Jolt::Jolt
)

set_project_warnings(engine_physics)

add_library(engine::physics ALIAS engine_physics)
