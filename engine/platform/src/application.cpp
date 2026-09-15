#include "platform/application.h"

#include "core/log.h"
#include "core/time.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

namespace platform {

Application::Application(ApplicationConfig config) : m_config(config) {}

bool Application::run() {
    if (!m_window.create(m_config.title, m_config.width, m_config.height)) {
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

        glClearColor(m_config.clearColor[0], m_config.clearColor[1], m_config.clearColor[2],
                     m_config.clearColor[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        onRender();
        m_window.swapBuffers();
    }

    m_window.destroy();
    core::logInfo("application shut down cleanly");
    return true;
}

} // namespace platform
