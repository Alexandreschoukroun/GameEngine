find_package(doctest CONFIG REQUIRED)

add_executable(engine_tests
    main.cpp
    core/test_time.cpp
    assets/test_gltf.cpp
    renderer/test_camera.cpp
    editor/test_gizmo.cpp
    scene/test_serialization.cpp
    scene/test_sector_graph.cpp
    physics/test_world.cpp
    physics/test_character.cpp
    physics/test_crouch.cpp
    physics/test_raycast.cpp
    physics/test_hinge.cpp
    physics/test_mesh_body.cpp
    scene/test_physics_sync.cpp
    scene/test_audio_sync.cpp
    scene/test_demo_scene.cpp
    scene/test_editing.cpp
    scene/test_occlusion.cpp
    scene/test_footsteps.cpp
    audio/test_engine.cpp
    audio/test_tension.cpp
)

# Ne sont liees ici que les couches testables sans GPU ni fenetre : c'est ce qui permet
# a la CI de lancer ces tests sur une machine sans carte graphique. L'audio en fait partie
# grace au backend silencieux de miniaudio, et platform n'y sert qu'a resoudre les chemins.
target_link_libraries(engine_tests PRIVATE engine::core engine::platform engine::assets engine::renderer engine::scene engine::physics engine::audio editor::editor doctest::doctest)

set_project_warnings(engine_tests)

include(CTest)
add_test(NAME engine_tests COMMAND engine_tests)
