#include "platform/application.h"

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M0";
    config.clearColor[0] = 0.04f;
    config.clearColor[1] = 0.0f;
    config.clearColor[2] = 0.02f;
    config.clearColor[3] = 1.0f;

    platform::Application app(config);
    return app.run() ? 0 : 1;
}
