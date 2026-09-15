#pragma once

#include "core/types.h"

#include <array>

namespace platform {

inline constexpr core::u32 kMaxKeys = 512;

class InputState {
public:
    bool isKeyDown(core::u32 scancode) const {
        return scancode < m_keysDown.size() && m_keysDown[scancode];
    }
    bool quitRequested() const { return m_quitRequested; }

private:
    friend class Input;
    std::array<bool, kMaxKeys> m_keysDown{};
    bool m_quitRequested = false;
};

class Input {
public:
    void update(InputState& state);
};

} // namespace platform
