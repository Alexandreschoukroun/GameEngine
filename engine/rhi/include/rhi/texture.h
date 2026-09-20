#pragma once

#include "core/types.h"

namespace rhi {

// Ce que les octets de la texture representent. Le GPU en a besoin pour savoir s'il doit
// convertir a l'echantillonnage.
enum class TextureFormat {
    // Couleur destinee a l'oeil : les octets suivent la courbe sRGB. Le GPU les convertit
    // en valeurs lineaires a chaque lecture, ce qui est indispensable pour calculer.
    SrgbColor,
    // Donnees techniques (rugosite, metallicite, carte de normales...) : les octets sont
    // deja proportionnels a la grandeur mesuree, aucune conversion ne doit avoir lieu.
    LinearData,
};

// Image stockee dans la memoire de la carte graphique, prete a etre lue par un shader.
// Les pixels attendus sont en RGBA 8 bits, ligne par ligne, du haut vers le bas.
class Texture {
public:
    Texture() = default;

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Envoie les pixels au GPU, genere les mipmaps et fixe le filtrage.
    bool create(core::u32 width, core::u32 height, const core::u8* pixelsRgba8,
                TextureFormat format);

    // Meme regle que Mesh et ShaderProgram : a appeler tant que le contexte GPU est vivant.
    void destroy();

    // Vrai si la texture existe sur le GPU. Utile pour les textures FACULTATIVES, comme
    // une carte de normales : l'appelant doit pouvoir savoir s'il a quelque chose a lier.
    bool isValid() const { return m_texture != 0; }

private:
    friend class Device;

    core::u32 m_texture = 0;
};

} // namespace rhi
