#pragma once

#include "core/types.h"

#include <string>

namespace scene {

class ResourceTable;
class Scene;

// Version du format. Un fichier ecrit par une version ulterieure sera refuse plutot que
// mal interprete : mieux vaut un message clair qu'un niveau silencieusement casse.
inline constexpr core::u32 kSceneFormatVersion = 1;

// Ecrit la scene en JSON. La sortie est DETERMINISTE : deux appels sur une meme scene
// produisent exactement les memes octets, quel que soit l'ordre de creation des entites.
//
// C'est l'exigence du SPEC, et elle n'est pas cosmetique : sans elle, chaque sauvegarde
// creerait un faux changement dans Git, les diffs seraient illisibles, et deux personnes
// travaillant sur des pieces differentes d'un meme niveau entreraient en conflit sur tout
// le fichier.
std::string saveSceneToString(const Scene& scene, const ResourceTable& resources);

// Meme chose, vers un fichier.
bool saveSceneToFile(const Scene& scene, const ResourceTable& resources, const char* path);

} // namespace scene
