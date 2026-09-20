find_package(imgui CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

add_library(editor STATIC
    src/editor.cpp
    src/gizmo.cpp
    src/panels.cpp
)

target_include_directories(editor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(editor
    PUBLIC
        # L'editeur edite une scene : ce type apparait dans son en-tete.
        engine::scene
    PRIVATE
        # PRIVATE, comme Jolt et miniaudio : ni ImGui ni SDL ne remontent au-dessus de
        # cette couche. L'en-tete public ne parle que de scene et de pointeurs opaques.
        engine::platform
        imgui::imgui
        SDL3::SDL3
)

set_project_warnings(editor)

add_library(editor::editor ALIAS editor)
