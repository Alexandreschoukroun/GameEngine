find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h" REQUIRED)

add_library(engine_audio STATIC
    src/engine.cpp
    src/tension.cpp
)

target_include_directories(engine_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# PRIVATE : aucun type de miniaudio ne remonte au-dessus de cette couche, comme pour Jolt
# dans engine::physics. L'API publique ne parle que de Vec3 et d'identifiants entiers.
target_include_directories(engine_audio PRIVATE
    ${MINIAUDIO_INCLUDE_DIRS}
)

target_link_libraries(engine_audio PUBLIC engine::core)

set_project_warnings(engine_audio)

add_library(engine::audio ALIAS engine_audio)
