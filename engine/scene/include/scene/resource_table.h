#pragma once

#include "audio/engine.h"
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

    const rhi::Mesh* mesh(ResourceHandle handle) const;
    const rhi::Texture* texture(ResourceHandle handle) const;
    audio::SoundHandle sound(ResourceHandle handle) const;

    // Nom logique, pour l'ecriture dans un fichier. Chaine vide si la poignee est invalide.
    std::string_view meshName(ResourceHandle handle) const;
    std::string_view textureName(ResourceHandle handle) const;
    std::string_view soundName(ResourceHandle handle) const;

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

    std::vector<MeshEntry> m_meshes;
    std::vector<TextureEntry> m_textures;
    std::vector<SoundEntry> m_sounds;
};

} // namespace scene
