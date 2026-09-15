#include "platform/input.h"

#include <SDL3/SDL.h>

namespace platform {

void Input::update(InputState& state) {
    state.m_quitRequested = false;

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
            default:
                break;
        }
    }
}

} // namespace platform
