find_package(glm CONFIG REQUIRED)

add_library(engine_core STATIC
    src/log.cpp
    src/time.cpp
)

target_include_directories(engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# PUBLIC, contrairement aux autres dependances : les types GLM apparaissent dans
# core/math.h, donc dans le vocabulaire public du moteur. C'est un choix explicite,
# documente dans math.h.
target_link_libraries(engine_core PUBLIC glm::glm)

target_compile_features(engine_core PUBLIC cxx_std_20)
set_project_warnings(engine_core)

add_library(engine::core ALIAS engine_core)
