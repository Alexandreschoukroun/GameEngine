find_package(Stb REQUIRED)

add_executable(pack_material main.cpp)

# stb_image_write n'est utilise que par cet outil : il n'a rien a faire dans le moteur.
target_include_directories(pack_material PRIVATE ${Stb_INCLUDE_DIR})

# On reutilise le chargeur d'images du moteur : l'outil lit donc exactement ce que le
# moteur sait lire, et les deux ne peuvent pas diverger.
target_link_libraries(pack_material PRIVATE engine::core engine::assets)

set_project_warnings(pack_material)
