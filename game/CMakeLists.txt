add_executable(game src/main.cpp)

target_link_libraries(game PRIVATE engine::platform engine::rhi engine::renderer engine::assets engine::scene engine::physics)

set_project_warnings(game)
