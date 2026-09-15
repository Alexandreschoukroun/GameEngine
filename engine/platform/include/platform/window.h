#pragma once

#include "core/types.h"

#include <string_view>

struct SDL_Window;

namespace platform {

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(std::string_view title, core::u32 width, core::u32 height);
    void destroy();

    void swapBuffers();

    core::u32 width() const { return m_width; }
    core::u32 height() const { return m_height; }

private:
    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    core::u32 m_width = 0;
    core::u32 m_height = 0;
};

} // namespace platform
