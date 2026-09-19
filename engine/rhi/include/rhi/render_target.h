#pragma once

#include "core/types.h"

namespace rhi {

// Cible de dessin autre que l'ecran : les fragments vont dans des textures, qu'on pourra
// relire ensuite. C'est ce qui permet le rendu differe.
//
// Disposition figee pour l'instant, celle du G-buffer :
//   attachement 0 : couleur de base, RGBA8      (4 octets/pixel)
//   attachement 1 : normale du monde, RGBA16F   (8 octets/pixel)
//   profondeur    : 24 bits, en texture pour etre relue
//
// La position n'y figure pas : elle se reconstruit depuis la profondeur et la matrice de
// camera. C'est la pratique standard, et trois canaux economises.
class RenderTarget {
public:
    RenderTarget() = default;

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    bool create(core::u32 width, core::u32 height);

    // Meme regle que partout dans rhi : a appeler tant que le contexte GPU est vivant.
    void destroy();

    core::u32 width() const { return m_width; }
    core::u32 height() const { return m_height; }

private:
    friend class Device;

    core::u32 m_framebuffer = 0;
    core::u32 m_albedoTexture = 0;
    core::u32 m_normalTexture = 0;
    core::u32 m_depthTexture = 0;
    core::u32 m_width = 0;
    core::u32 m_height = 0;
};

} // namespace rhi
