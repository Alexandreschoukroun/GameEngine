#pragma once

#include "audio/engine.h"
#include "core/math.h"
#include "core/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace rhi {
class Mesh;
class Texture;
} // namespace rhi

namespace scene {


// Une poignee designe une ressource sans pointer dessus. Un pointeur est une adresse
// memoire : il change a chaque lancement, donc il ne peut pas etre ecrit dans un fichier.
using ResourceHandle = core::u32;
inline constexpr ResourceHandle kInvalidResource = 0xFFFFFFFFu;

// Une matiere, telle qu'un fichier glTF la decrit : jusqu'a trois textures, et des
// facteurs qui les multiplient.
//
// Regrouper ces six informations sous un nom a deux effets. D'abord le fichier de scene
// cite UNE matiere au lieu de trois textures, ce qui est a la fois plus court et plus
// juste - "du bois", pas "cette couleur avec ce relief". Ensuite c'est l'unite que
// l'editeur de M6 manipulera, et celle que les modeles importes apportent avec eux.
struct Material {
    ResourceHandle baseColor = kInvalidResource;
    ResourceHandle metallicRoughness = kInvalidResource;
    // Facultative : sans elle, la surface s'eclaire par sa seule geometrie.
    ResourceHandle normalMap = kInvalidResource;

    // Multiplient les textures. Valent 1 quand le fichier n'en dit rien, si bien qu'un
    // materiau sans carte de couleur mais avec un facteur rouge decrit un objet rouge uni.
    core::Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    core::f32 metallicFactor = 1.0f;
    core::f32 roughnessFactor = 1.0f;
};

// Geometrie de collision : des triangles, sans normales ni UV.
//
// C'est volontairement une VUE et non une copie : la table ne possede rien, elle
// reference, exactement comme pour les maillages GPU. Les donnees vivent la ou le jeu les
// a chargees, et doivent lui survivre.
//
// Elle est separee du maillage d'affichage parce que les deux n'ont pas la meme forme :
// la collision d'un decor est toujours plus grossiere que sa geometrie visible. Ici elles
// coincident, mais rien n'y oblige.
struct CollisionMesh {
    const core::Vec3* positions = nullptr;
    core::u32 vertexCount = 0;
    const core::u32* indices = nullptr;
    core::u32 indexCount = 0;

    bool isValid() const {
        return positions != nullptr && indices != nullptr && vertexCount > 0 &&
               indexCount >= 3 && indexCount % 3 == 0;
    }
};

// Fait la correspondance entre les noms logiques ecrits dans les fichiers de scene
// ("suzanne", "damier") et les ressources GPU chargees.
//
// Consequence utile : renommer un fichier sur le disque ne casse aucune scene, puisque les
// scenes citent un nom logique et non un chemin. Et l'editeur de M6 pourra lister ce qui
// est disponible en parcourant cette table.
class ResourceTable {
public:
    // Enregistre une ressource deja chargee. La table ne possede rien : elle reference.
    ResourceHandle addMesh(std::string_view name, const rhi::Mesh* mesh);
    ResourceHandle addTexture(std::string_view name, const rhi::Texture* texture);
    // Un son n'est pas une ressource GPU, mais la table lui rend le meme service : la
    // scene cite "braises" et ignore quel fichier c'est, comme elle cite "suzanne".
    ResourceHandle addSound(std::string_view name, audio::SoundHandle sound);

    ResourceHandle findMesh(std::string_view name) const;
    ResourceHandle findTexture(std::string_view name) const;
    ResourceHandle findSound(std::string_view name) const;
    ResourceHandle addMaterial(std::string_view name, const Material& material);
    ResourceHandle addCollisionMesh(std::string_view name, const CollisionMesh& mesh);
    ResourceHandle findMaterial(std::string_view name) const;
    ResourceHandle findCollisionMesh(std::string_view name) const;

    const rhi::Mesh* mesh(ResourceHandle handle) const;
    const rhi::Texture* texture(ResourceHandle handle) const;
    audio::SoundHandle sound(ResourceHandle handle) const;
    // Nul si la poignee est invalide : l'appelant saute alors l'objet plutot que de
    // dessiner une matiere inventee.
    const Material* material(ResourceHandle handle) const;
    const CollisionMesh* collisionMesh(ResourceHandle handle) const;

    // Nom logique, pour l'ecriture dans un fichier. Chaine vide si la poignee est invalide.
    std::string_view meshName(ResourceHandle handle) const;
    std::string_view textureName(ResourceHandle handle) const;
    std::string_view soundName(ResourceHandle handle) const;
    std::string_view materialName(ResourceHandle handle) const;
    std::string_view collisionMeshName(ResourceHandle handle) const;

private:
    struct MeshEntry {
        std::string name;
        const rhi::Mesh* mesh = nullptr;
    };
    struct TextureEntry {
        std::string name;
        const rhi::Texture* texture = nullptr;
    };

    struct SoundEntry {
        std::string name;
        audio::SoundHandle sound = audio::kInvalidSound;
    };

    struct MaterialEntry {
        std::string name;
        Material material;
    };

    std::vector<MeshEntry> m_meshes;
    std::vector<TextureEntry> m_textures;
    std::vector<SoundEntry> m_sounds;
    struct CollisionMeshEntry {
        std::string name;
        CollisionMesh mesh;
    };

    std::vector<MaterialEntry> m_materials;
    std::vector<CollisionMeshEntry> m_collisionMeshes;
};

} // namespace scene
