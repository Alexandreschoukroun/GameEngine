#include "platform/input.h"

#include <SDL3/SDL.h>

namespace platform {
namespace {

// Traduction du vocabulaire du moteur vers les positions physiques de SDL. C'est le seul
// endroit du projet qui connait les scancodes SDL.
SDL_Scancode toScancode(Key key) {
    switch (key) {
        case Key::W: return SDL_SCANCODE_W;
        case Key::A: return SDL_SCANCODE_A;
        case Key::S: return SDL_SCANCODE_S;
        case Key::D: return SDL_SCANCODE_D;
        case Key::Space: return SDL_SCANCODE_SPACE;
        case Key::LeftShift: return SDL_SCANCODE_LSHIFT;
        case Key::Tab: return SDL_SCANCODE_TAB;
        case Key::E: return SDL_SCANCODE_E;
        case Key::F: return SDL_SCANCODE_F;
        case Key::F5: return SDL_SCANCODE_F5;
    }
    return SDL_SCANCODE_UNKNOWN;
}

} // namespace

bool InputState::isKeyDown(Key key) const {
    const auto scancode = static_cast<core::u32>(toScancode(key));
    return scancode < m_keysDown.size() && m_keysDown[scancode];
}

void Input::update(InputState& state) {
    state.m_quitRequested = false;
    // Les deplacements de souris sont consommes a chaque frame : ils decrivent ce qui
    // s'est passe depuis la precedente, pas un etat durable comme une touche enfoncee.
    state.m_mouseDeltaX = 0.0f;
    state.m_mouseDeltaY = 0.0f;
    state.m_windowResized = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                state.m_quitRequested = true;
                break;
            case SDL_EVENT_KEY_DOWN: {
                const auto scancode = static_cast<core::u32>(event.key.scancode);
                if (scancode < state.m_keysDown.size()) {
                    state.m_keysDown[scancode] = true;
                }
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    state.m_quitRequested = true;
                }
                break;
            }
            case SDL_EVENT_KEY_UP: {
                const auto scancode = static_cast<core::u32>(event.key.scancode);
                if (scancode < state.m_keysDown.size()) {
                    state.m_keysDown[scancode] = false;
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                // xrel/yrel : le deplacement, et non la position. Y croit vers le bas.
                state.m_mouseDeltaX += event.motion.xrel;
                state.m_mouseDeltaY += event.motion.yrel;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                // Taille en pixels reels, et non taille logique de la fenetre : avec une
                // mise a l'echelle Windows a 150 %, les deux different, et c'est celle-ci
                // qui interesse le GPU.
                state.m_windowResized = true;
                state.m_windowWidth = static_cast<core::u32>(event.window.data1);
                state.m_windowHeight = static_cast<core::u32>(event.window.data2);
                break;
            default:
                break;
        }
    }
}

} // namespace platform
