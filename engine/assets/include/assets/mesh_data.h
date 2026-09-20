#pragma once

#include "core/math.h"
#include "core/types.h"

#include <string>
#include <vector>

namespace assets {

// Matiere d'une surface, telle que le fichier glTF la decrit.
//
// Les FACTEURS multiplient toujours, meme sans texture : c'est la convention du format.
// Un materiau sans carte de couleur mais avec un baseColorFactor rouge est donc un objet
// rouge uni - c'est ainsi que sont faits la plupart des modeles simples.
//
// Les chemins de texture sont relatifs au fichier glTF, et resolus par le chargeur en
// chemins complets : l'appelant n'a pas a savoir ou le modele vivait.
struct MaterialData {
    std::string name;
    core::Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    core::f32 metallicFactor = 1.0f;
    core::f32 roughnessFactor = 1.0f;
    // Amplitude du relief, que glTF autorise a doser par materiau.
    core::f32 normalScale = 1.0f;

    std::string baseColorTexture;
    std::string metallicRoughnessTexture;
    std::string normalTexture;
};

// Une portion du maillage qui partage un meme materiau.
//
// Un modele telecharge en compte presque toujours plusieurs : un personnage a une peau,
// des yeux et des vetements. Les fusionner en un seul morceau, comme le moteur le faisait,
// revenait a leur imposer une seule matiere - donc a afficher les yeux en tissu.
struct SubMesh {
    core::u32 firstIndex = 0;
    core::u32 indexCount = 0;
    // Indice dans MeshData::materials, ou kNoMaterial.
    core::u32 material = 0xFFFFFFFFu;
};

inline constexpr core::u32 kNoMaterial = 0xFFFFFFFFu;

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
    // Les portions du maillage et les matieres qu'elles utilisent. Un seul tampon de
    // sommets sert a toutes : ce qui change d'une portion a l'autre, c'est la plage
    // d'indices a dessiner et le materiau a lier.
    std::vector<SubMesh> subMeshes;
    std::vector<MaterialData> materials;

    bool isValid() const {
        return !positions.empty() && positions.size() == normals.size() &&
               positions.size() == uvs.size() && !indices.empty() &&
               // Les tangentes sont facultatives, mais si elles sont la, il en faut une
               // par sommet : un tableau partiel donnerait un relief sur une moitie du
               // maillage seulement.
               (tangents.empty() || tangents.size() == positions.size());
    }

    bool hasTangents() const { return tangents.size() == positions.size(); }

    // Boite englobante alignee sur les axes, dans le repere du modele.
    //
    // Elle sert a designer un objet du bout de la souris : tester un rayon contre une
    // boite coute quelques comparaisons, contre chaque triangle cent mille fois plus.
    // Pour choisir un objet, la boite suffit largement - on ne demande pas au pixel pres
    // quel objet on vise, on demande lequel est devant.
    //
    // Rend faux si le maillage est vide.
    bool computeBounds(core::Vec3& outMin, core::Vec3& outMax) const;
};

// Calcule des tangentes a partir des positions et des coordonnees de texture.
//
// A quoi ca sert : glTF n'OBLIGE pas un fichier a fournir ses tangentes, meme quand il
// declare une carte de normales - la specification se contente de dire que le lecteur
// devrait les calculer. Beaucoup de modeles telecharges sont dans ce cas. Sans tangente,
// le repere de l'espace tangent est nul, et le shader calculerait normalize(0) : un
// eclairage casse, pas simplement moins beau.
//
// La methode est celle de tout le monde : chaque triangle donne la direction dans laquelle
// U augmente, deduite de ses aretes et de leurs differences d'UV ; chaque sommet accumule
// les contributions des triangles qui le partagent, puis on normalise. Ce n'est pas
// MikkTSpace a l'identique - le resultat peut differer sur les coutures - mais c'est
// exact partout ailleurs, et infiniment mieux que rien.
//
// Sans coordonnees de texture, il n'y a aucune information a exploiter : la fonction rend
// faux et laisse le maillage sans tangentes.
bool generateTangents(MeshData& mesh);

// Charge la geometrie d'un fichier glTF 2.0 (.gltf avec son .bin a cote).
//
// Toutes les primitives triangulaires de toutes les scenes sont fusionnees en un seul
// maillage, transformees par la matrice monde de leur noeud. Les tangentes sont lues si
// le fichier en fournit ; sinon le maillage s'eclaire sans relief.
bool loadGltfMesh(const char* path, MeshData& out);

} // namespace assets
