#include "platform/window.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <string>

namespace platform {
namespace {

void* glGetProcAddress(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

} // namespace

Window::~Window() {
    destroy();
}

bool Window::create(std::string_view title, core::u32 width, core::u32 height) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        core::logError(SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    // Tampon de profondeur : 24 bits par pixel. Sans cette demande, le contexte peut etre
    // cree sans profondeur du tout, et le test de profondeur n'aurait alors aucun effet.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#if !defined(NDEBUG)
    // Contexte de debug : le pilote produit alors des messages d'erreur detailles,
    // que rhi::Device redirige vers nos logs. Inutile en Release.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

    // REDIMENSIONNABLE : sans ce drapeau la fenetre est figee a sa taille de
    // creation, et le chemin de redimensionnement du moteur - qui existe pourtant
    // jusqu'au G-buffer - ne pouvait jamais etre emprunte.
    m_window = SDL_CreateWindow(std::string(title).c_str(), static_cast<int>(width),
                                 static_cast<int>(height),
                                 SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        core::logError(SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        core::logError(SDL_GetError());
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    SDL_GL_SetSwapInterval(1);

    m_width = width;
    m_height = height;
    core::logInfo("window created");
    return true;
}

void Window::destroy() {
    if (m_glContext) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_glContext));
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        core::logInfo("window destroyed");
    }
}

void Window::swapBuffers() {
    SDL_GL_SwapWindow(m_window);
}

GLProcAddressLoader Window::glProcAddressLoader() const {
    return &glGetProcAddress;
}

void* Window::nativeWindow() const { return m_window; }

void Window::setRelativeMouseMode(bool enabled) {
    if (m_window != nullptr) {
        SDL_SetWindowRelativeMouseMode(m_window, enabled);
    }
}

bool Window::setFullscreen(bool fullscreen) {
    if (m_window == nullptr || fullscreen == m_fullscreen) {
        return false;
    }
    if (!SDL_SetWindowFullscreen(static_cast<SDL_Window*>(m_window), fullscreen)) {
        core::logError(SDL_GetError());
        return false;
    }
    m_fullscreen = fullscreen;
    // On ne met PAS a jour m_width et m_height ici : SDL enverra son evenement de
    // redimensionnement, et le prendre en avance donnerait une taille juste avant que le
    // G-buffer ne soit refait - donc une frame dessinee a la mauvaise echelle.
    return true;
}

void Window::notifyResized(core::u32 width, core::u32 height) {
    m_width = width;
    m_height = height;
}

} // namespace platform
