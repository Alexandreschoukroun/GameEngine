#pragma once

#include "core/math.h"
#include "core/types.h"

#include <vector>

namespace assets {

// Geometrie telle qu'elle sort d'un fichier : des donnees en RAM, rien de plus.
//
// Volontairement ignorante de rhi : un chargeur de fichiers n'a aucune raison de savoir
// ce qu'est un buffer OpenGL. C'est l'appelant qui convertit vers le format de sommet du
// moteur et envoie au GPU.
struct MeshData {
    std::vector<core::Vec3> positions;
    std::vector<core::Vec2> uvs;
    std::vector<core::u32> indices;

    bool isValid() const {
        return !positions.empty() && positions.size() == uvs.size() && !indices.empty();
    }
};

// Charge la geometrie d'un fichier glTF 2.0 (.gltf avec son .bin a cote).
//
// Toutes les primitives triangulaires de toutes les scenes sont fusionnees en un seul
// maillage, chaque position etant transformee par la matrice monde de son noeud. Les
// normales et les tangentes du fichier sont ignorees : rien ne les utilise encore.
bool loadGltfMesh(const char* path, MeshData& out);

} // namespace assets
