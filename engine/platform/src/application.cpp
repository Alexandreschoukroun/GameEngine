#include "platform/application.h"

#include "core/log.h"
#include "core/time.h"

namespace platform {

Application::Application(ApplicationConfig config) : m_config(config) {}

bool Application::run() {
    if (!m_window.create(m_config.title, m_config.width, m_config.height)) {
        return false;
    }

    if (!onInit()) {
        m_window.destroy();
        core::logError("initialisation de l'application echouee");
        return false;
    }

    core::Clock clock;
    core::FixedTimestepAccumulator accumulator(m_config.fixedTimestepSeconds);

    bool running = true;
    while (running) {
        m_input.update(m_inputState);
        if (m_inputState.quitRequested()) {
            break;
        }

        accumulator.addFrameTime(clock.restart());
        while (accumulator.consumeStep()) {
            onFixedUpdate(m_config.fixedTimestepSeconds);
        }

        onRender();
        m_window.swapBuffers();
    }

    onShutdown();
    m_window.destroy();
    core::logInfo("application shut down cleanly");
    return true;
}

} // namespace platform
