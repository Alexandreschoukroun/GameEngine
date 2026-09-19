find_package(doctest CONFIG REQUIRED)

add_executable(engine_tests
    main.cpp
    core/test_time.cpp
    renderer/test_camera.cpp
    scene/test_serialization.cpp
    scene/test_sector_graph.cpp
    physics/test_world.cpp
    physics/test_character.cpp
    physics/test_raycast.cpp
    physics/test_hinge.cpp
    scene/test_physics_sync.cpp
    audio/test_engine.cpp
)

# Ne sont liees ici que les couches testables sans GPU ni fenetre : c'est ce qui permet
# a la CI de lancer ces tests sur une machine sans carte graphique. L'audio en fait partie
# grace au backend silencieux de miniaudio, et platform n'y sert qu'a resoudre les chemins.
target_link_libraries(engine_tests PRIVATE engine::core engine::platform engine::renderer engine::scene engine::physics engine::audio doctest::doctest)

set_project_warnings(engine_tests)

include(CTest)
add_test(NAME engine_tests COMMAND engine_tests)
