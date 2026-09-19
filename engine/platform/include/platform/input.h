#pragma once

#include "core/types.h"

#include <array>

namespace platform {

inline constexpr core::u32 kMaxKeys = 512;

// Touches exposees au moteur. Volontairement reduit a ce qui est utilise : on ajoutera au
// fur et a mesure des besoins reels.
//
// Ces valeurs designent des POSITIONS PHYSIQUES, pas les symboles imprimes sur les touches.
// Key::W est donc la touche marquee Z sur un clavier AZERTY : les commandes sont ZQSD en
// France et WASD ailleurs, sans aucune configuration. Le remappage complet viendra avec la
// couche accessibilite (M8).
enum class Key : core::u32 {
    W,
    A,
    S,
    D,
    Space,
    LeftShift,
    Tab,
    E,
    F,
    F5,
};

class InputState {
public:
    bool isKeyDown(Key key) const;
    bool quitRequested() const { return m_quitRequested; }

    // Deplacement de la souris depuis la frame precedente, en pixels. En mode relatif, il
    // n'est borne par rien : le curseur ne bute sur aucun bord d'ecran.
    core::f32 mouseDeltaX() const { return m_mouseDeltaX; }
    core::f32 mouseDeltaY() const { return m_mouseDeltaY; }

    // Vrai pendant la frame ou la fenetre a change de taille, en pixels reels.
    bool windowResized() const { return m_windowResized; }
    core::u32 windowWidth() const { return m_windowWidth; }
    core::u32 windowHeight() const { return m_windowHeight; }

private:
    friend class Input;

    std::array<bool, kMaxKeys> m_keysDown{};
    bool m_quitRequested = false;
    core::f32 m_mouseDeltaX = 0.0f;
    core::f32 m_mouseDeltaY = 0.0f;
    bool m_windowResized = false;
    core::u32 m_windowWidth = 0;
    core::u32 m_windowHeight = 0;
};

class Input {
public:
    void update(InputState& state);
};

} // namespace platform
