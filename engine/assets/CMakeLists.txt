find_path(CGLTF_INCLUDE_DIRS "cgltf.h" REQUIRED)
find_package(Stb REQUIRED)

add_library(engine_assets STATIC
    src/gltf_loader.cpp
    src/image.cpp
    src/text_file.cpp
)

target_include_directories(engine_assets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# PRIVATE : cgltf et stb_image ne remontent pas au-dessus de cette couche. Les chargeurs
# rendent des donnees neutres (positions, UV, indices, pixels), pas des types tiers.
target_include_directories(engine_assets PRIVATE
    ${CGLTF_INCLUDE_DIRS}
    ${Stb_INCLUDE_DIR}
)

# assets ne connait ni le GPU ni la fenetre : lire un fichier n'a rien a voir avec rhi.
target_link_libraries(engine_assets PUBLIC engine::core)

set_project_warnings(engine_assets)

add_library(engine::assets ALIAS engine_assets)
