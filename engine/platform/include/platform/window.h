#pragma once

#include "core/types.h"

#include <string_view>

struct SDL_Window;

namespace platform {

// Fonction qui rend l'adresse d'une fonction OpenGL a partir de son nom. On la fournit aux
// couches superieures pour qu'elles chargent OpenGL sans jamais voir SDL.
using GLProcAddressLoader = void* (*)(const char* name);

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(std::string_view title, core::u32 width, core::u32 height);
    void destroy();

    void swapBuffers();

    GLProcAddressLoader glProcAddressLoader() const;

    core::u32 width() const { return m_width; }
    core::u32 height() const { return m_height; }

private:
    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    core::u32 m_width = 0;
    core::u32 m_height = 0;
};

} // namespace platform
