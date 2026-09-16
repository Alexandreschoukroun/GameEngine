#include "platform/application.h"

#include "core/log.h"
#include "core/profiler.h"
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
        {
            ENGINE_PROFILE_SCOPE("input");
            m_input.update(m_inputState);
        }
        if (m_inputState.quitRequested()) {
            break;
        }

        if (m_inputState.windowResized()) {
            m_window.notifyResized(m_inputState.windowWidth(), m_inputState.windowHeight());
            onResize(m_inputState.windowWidth(), m_inputState.windowHeight());
        }

        const core::f64 frameSeconds = clock.restart();
        {
            ENGINE_PROFILE_SCOPE("frame update");
            onFrame(frameSeconds);
        }

        {
            // Une zone pour l'ensemble des pas : dans Tracy, sa largeur montre d'un coup
            // d'oeil combien de pas fixes la frame a consommes.
            ENGINE_PROFILE_SCOPE("fixed update");
            accumulator.addFrameTime(frameSeconds);
            while (accumulator.consumeStep()) {
                onFixedUpdate(m_config.fixedTimestepSeconds);
            }
        }

        {
            ENGINE_PROFILE_SCOPE("render");
            onRender();
        }
        {
            // Avec la synchronisation verticale, c'est ici que la frame attend l'ecran.
            // Une zone large ne veut donc pas dire "lent", mais "en avance".
            ENGINE_PROFILE_SCOPE("present");
            m_window.swapBuffers();
        }

        ENGINE_PROFILE_FRAME();
    }

    onShutdown();
    m_window.destroy();
    core::logInfo("application shut down cleanly");
    return true;
}

} // namespace platform
