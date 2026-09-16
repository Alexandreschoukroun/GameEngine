#pragma once

#include "core/types.h"

namespace rhi {

// Image stockee dans la memoire de la carte graphique, prete a etre lue par un shader.
// Les pixels attendus sont en RGBA 8 bits, ligne par ligne, du haut vers le bas.
class Texture {
public:
    Texture() = default;

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Envoie les pixels au GPU, genere les mipmaps et fixe le filtrage.
    bool create(core::u32 width, core::u32 height, const core::u8* pixelsRgba8);

    // Meme regle que Mesh et ShaderProgram : a appeler tant que le contexte GPU est vivant.
    void destroy();

private:
    friend class Device;

    core::u32 m_texture = 0;
};

} // namespace rhi
