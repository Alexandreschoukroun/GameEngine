add_executable(game src/main.cpp)

target_link_libraries(game PRIVATE engine::platform engine::rhi)

set_project_warnings(game)
