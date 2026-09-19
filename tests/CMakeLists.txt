find_package(doctest CONFIG REQUIRED)

add_executable(engine_tests
    main.cpp
    core/test_time.cpp
    renderer/test_camera.cpp
    scene/test_serialization.cpp
    scene/test_sector_graph.cpp
)

# Ne sont liees ici que les couches testables sans GPU ni fenetre : c'est ce qui permet
# a la CI de lancer ces tests sur une machine sans carte graphique.
target_link_libraries(engine_tests PRIVATE engine::core engine::renderer engine::scene doctest::doctest)

set_project_warnings(engine_tests)

include(CTest)
add_test(NAME engine_tests COMMAND engine_tests)
