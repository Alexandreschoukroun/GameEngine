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

    // Capture et masque le curseur : la souris ne renvoie plus que des deplacements,
    // sans bord d'ecran. C'est le mode de tous les jeux a la premiere personne.
    void setRelativeMouseMode(bool enabled);

    // Enregistre la nouvelle taille apres un redimensionnement. Ne redimensionne pas la
    // fenetre : c'est le systeme qui l'a deja fait, on se met a jour.
    void notifyResized(core::u32 width, core::u32 height);

    // Poignees natives, volontairement OPAQUES : un SDL_Window* et un SDL_GLContext.
    //
    // Seul l'editeur s'en sert, pour brancher Dear ImGui qui a besoin de la vraie fenetre.
    // Les rendre en void* respecte la regle 2 du SPEC a la lettre : aucun type SDL
    // n'apparait ici, et personne ne peut s'en servir par accident - il faut savoir ce
    // qu'on caste pour en faire quoi que ce soit.
    void* nativeWindow() const;
    void* nativeGlContext() const { return m_glContext; }

    // Plein ecran sans changer de mode d'affichage : la fenetre couvre l'ecran a sa
    // resolution native, sans bordure. C'est ce qu'on veut ici pour trois raisons - le
    // basculement est instantane, les autres fenetres ne sont pas deplacees par un
    // changement de mode, et une capture video y trouve la pleine resolution.
    //
    // Le moteur n'a rien d'autre a faire : SDL emet un evenement de redimensionnement, et
    // le chemin qui va jusqu'au G-buffer existe deja.
    bool setFullscreen(bool fullscreen);
    void toggleFullscreen() { setFullscreen(!m_fullscreen); }
    bool isFullscreen() const { return m_fullscreen; }

    core::u32 width() const { return m_width; }
    core::u32 height() const { return m_height; }

private:
    SDL_Window* m_window = nullptr;
    void* m_glContext = nullptr;
    bool m_fullscreen = false;
    core::u32 m_width = 0;
    core::u32 m_height = 0;
};

} // namespace platform
