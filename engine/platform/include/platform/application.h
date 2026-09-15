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
    core::f32 clearColor[4] = {0.02f, 0.0f, 0.0f, 1.0f};
};

class Application {
public:
    explicit Application(ApplicationConfig config = {});
    virtual ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool run();

protected:
    virtual void onFixedUpdate(core::f64 fixedDeltaSeconds) { (void)fixedDeltaSeconds; }
    virtual void onRender() {}

private:
    ApplicationConfig m_config;
    Window m_window;
    Input m_input;
    InputState m_inputState;
};

} // namespace platform
