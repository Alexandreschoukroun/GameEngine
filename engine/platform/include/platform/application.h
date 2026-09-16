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
    virtual void onFixedUpdate(core::f64 fixedDeltaSeconds) { (void)fixedDeltaSeconds; }
    virtual void onRender() {}
    // Appele avant la destruction de la fenetre : le contexte GPU est encore valide.
    virtual void onShutdown() {}

    Window& window() { return m_window; }

private:
    ApplicationConfig m_config;
    Window m_window;
    Input m_input;
    InputState m_inputState;
};

} // namespace platform
