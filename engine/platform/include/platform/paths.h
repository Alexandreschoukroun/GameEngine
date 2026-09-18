#pragma once

#include <string>
#include <string_view>

namespace platform {

// Racine des donnees du jeu, resolue une fois au premier appel, dans cet ordre :
//   1. la variable d'environnement GAMEENGINE_ASSETS, si elle designe un dossier existant ;
//   2. un dossier "assets" a cote de l'executable   -> le cas du jeu distribue ;
//   3. le dossier assets/ des sources, inscrit par CMake -> le cas du developpement.
//
// Un seul code couvre donc les deux situations, et modifier un fichier de donnees ne
// demande aucune recompilation.
// Rend une chaine vide si aucune racine n'a ete trouvee.
const std::string& assetsRoot();

// Chemin complet d'un fichier de donnees, par exemple "models/suzanne/Suzanne.gltf".
std::string assetPath(std::string_view relativePath);

} // namespace platform
