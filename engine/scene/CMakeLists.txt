find_package(EnTT CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(engine_scene STATIC
    src/physics_sync.cpp
    src/resource_table.cpp
    src/scene.cpp
    src/sector_graph.cpp
    src/serialization.cpp
)

target_include_directories(engine_scene PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(engine_scene
    PUBLIC
        engine::core
        # Les composants decrivent des lumieres et referencent des ressources GPU : ces
        # types apparaissent donc dans les en-tetes de la scene.
        engine::renderer
        # La scene decrit les colliders et synchronise les poses : le SPEC place scene,
        # physique, audio et IA dans la meme bande, ces systemes se parlent par nature.
        # Ce qui reste interdit, c'est qu'un type Jolt remonte - le PIMPL s'en charge.
        engine::physics
        # PUBLIC aussi : entt::entity et entt::registry font partie du vocabulaire expose.
        # Troisieme et derniere exception a la regle 2, assumee comme GLM et Tracy - il n'y
        # aura pas de second ECS a substituer, la stack du SPEC le verrouille.
        EnTT::EnTT
    PRIVATE
        # PRIVATE : le format des fichiers est un detail d'implementation. Personne
        # au-dessus de cette couche ne manipule de json.
        nlohmann_json::nlohmann_json
)

set_project_warnings(engine_scene)

add_library(engine::scene ALIAS engine_scene)
