#include "platform/application.h"
#include "rhi/device.h"

namespace {

class HorrorGame final : public platform::Application {
public:
    using Application::Application;

protected:
    bool onInit() override {
        if (!m_device.create(window().glProcAddressLoader())) {
            return false;
        }
        m_device.setViewport(window().width(), window().height());
        return true;
    }

    void onRender() override { m_device.clear(0.04f, 0.0f, 0.02f, 1.0f); }

private:
    rhi::Device m_device;
};

} // namespace

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M1";

    HorrorGame game(config);
    return game.run() ? 0 : 1;
}
