#pragma once

#include "core/types.h"
#include "platform/input.h"
#include "platform/window.h"

#include <string_view>

namespace platform {

struct ApplicationConfig {
    std::string_view title = "GameEngine";
    core::u32 width = 1280;
    core::u32 height = 720;
    core::f64 fixedTimestepSeconds = 1.0 / 60.0;
};

class Application {
public:
    explicit Application(ApplicationConfig config = {});
    virtual ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool run();

protected:
    // Appele une fois, la fenetre et le contexte GPU etant crees. Renvoyer false annule
    // le demarrage. C'est ici que le jeu initialise sa couche de rendu.
    virtual bool onInit() { return true; }

    // Appele exactement une fois par frame, avec le temps reel ecoule. C'est la place des
    // entrees directes, comme le regard a la souris : les appliquer dans onFixedUpdate les
    // doublerait sur une frame lente et les perdrait sur une frame rapide.
    virtual void onFrame(core::f64 frameDeltaSeconds) { (void)frameDeltaSeconds; }

    // Appele 0, 1 ou plusieurs fois par frame, toujours avec le meme pas : c'est la place
    // de tout ce qui doit rester deterministe (deplacement, physique, logique de jeu).
    virtual void onFixedUpdate(core::f64 fixedDeltaSeconds) { (void)fixedDeltaSeconds; }
    virtual void onRender() {}

    // Appele quand la fenetre a change de taille, en pixels reels.
    virtual void onResize(core::u32 width, core::u32 height) {
        (void)width;
        (void)height;
    }
    // Appele avant la destruction de la fenetre : le contexte GPU est encore valide.
    virtual void onShutdown() {}

    Window& window() { return m_window; }
    const InputState& input() const { return m_inputState; }

private:
    ApplicationConfig m_config;
    Window m_window;
    Input m_input;
    InputState m_inputState;
};

} // namespace platform
