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
    std::vector<core::Vec3> normals;
    // Tangente en quatre composantes, comme glTF l'impose : xyz donne la direction dans
    // laquelle U augmente sur la surface, et w vaut +1 ou -1 selon l'orientation de la
    // carte UV. Sans ce signe, une texture miroitee produirait un relief inverse - un
    // creux la ou il faut une bosse.
    //
    // Vide si le fichier n'en fournit pas : la surface est alors eclairee par sa seule
    // normale geometrique, sans relief.
    std::vector<core::Vec4> tangents;
    std::vector<core::Vec2> uvs;
    std::vector<core::u32> indices;

    bool isValid() const {
        return !positions.empty() && positions.size() == normals.size() &&
               positions.size() == uvs.size() && !indices.empty() &&
               // Les tangentes sont facultatives, mais si elles sont la, il en faut une
               // par sommet : un tableau partiel donnerait un relief sur une moitie du
               // maillage seulement.
               (tangents.empty() || tangents.size() == positions.size());
    }

    bool hasTangents() const { return tangents.size() == positions.size(); }
};

// Charge la geometrie d'un fichier glTF 2.0 (.gltf avec son .bin a cote).
//
// Toutes les primitives triangulaires de toutes les scenes sont fusionnees en un seul
// maillage, transformees par la matrice monde de leur noeud. Les tangentes sont lues si
// le fichier en fournit ; sinon le maillage s'eclaire sans relief.
bool loadGltfMesh(const char* path, MeshData& out);

} // namespace assets
