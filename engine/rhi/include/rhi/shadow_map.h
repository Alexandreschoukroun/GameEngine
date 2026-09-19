#pragma once

#include "core/types.h"

namespace rhi {

// Carte de profondeur vue depuis une lumiere. Aucune couleur n'y est ecrite : seule la
// distance du premier obstacle dans chaque direction est enregistree.
//
// La texture est configuree en mode comparaison : le GPU ne rend pas la profondeur
// stockee, il rend directement le resultat du test "ce point est-il devant ?", et il le
// fait dans le filtrage de texture, donc gratuitement adouci.
class ShadowMap {
public:
    ShadowMap() = default;

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Carre, en general 1024 ou 2048. Plus grand = ombres plus nettes, mais 4 fois plus
    // de memoire a chaque doublement.
    bool create(core::u32 resolution);

    // Meme regle que partout dans rhi : a appeler tant que le contexte GPU est vivant.
    void destroy();

    core::u32 resolution() const { return m_resolution; }

private:
    friend class Device;

    core::u32 m_framebuffer = 0;
    core::u32 m_depthTexture = 0;
    core::u32 m_resolution = 0;
};

} // namespace rhi
