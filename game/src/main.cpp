#include "core/types.h"
#include "platform/application.h"
#include "rhi/device.h"
#include "rhi/mesh.h"
#include "rhi/shader_program.h"

#include <iterator>

namespace {

// Sommets ecrits directement en coordonnees normalisees (NDC) : X et Y vont de -1 a +1,
// quelle que soit la taille de la fenetre. Il n'y a pas encore de camera pour convertir
// des coordonnees du monde vers cet espace, ce sera l'etape 4.
constexpr rhi::Vertex kTriangle[] = {
    {{0.0f, 0.6f, 0.0f}},
    {{-0.6f, -0.5f, 0.0f}},
    {{0.6f, -0.5f, 0.0f}},
};

// GLSL, compile par le pilote au demarrage du jeu. Ces sources partiront dans des fichiers
// quand la couche assets existera.
constexpr const char* kVertexShader = R"(#version 460 core
layout(location = 0) in vec3 aPosition;

void main() {
    gl_Position = vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 460 core
out vec4 outColor;

void main() {
    outColor = vec4(0.72, 0.68, 0.62, 1.0);
}
)";

class HorrorGame final : public platform::Application {
public:
    using Application::Application;

protected:
    bool onInit() override {
        if (!m_device.create(window().glProcAddressLoader())) {
            return false;
        }
        m_device.setViewport(window().width(), window().height());

        if (!m_program.create(kVertexShader, kFragmentShader)) {
            return false;
        }
        return m_mesh.create(kTriangle, static_cast<core::u32>(std::size(kTriangle)));
    }

    void onRender() override {
        m_device.clear(0.04f, 0.0f, 0.02f, 1.0f);
        m_device.draw(m_program, m_mesh);
    }

    // Le contexte GPU est encore vivant ici : c'est le seul endroit ou liberer ces objets.
    void onShutdown() override {
        m_mesh.destroy();
        m_program.destroy();
    }

private:
    rhi::Device m_device;
    rhi::ShaderProgram m_program;
    rhi::Mesh m_mesh;
};

} // namespace

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M1";

    HorrorGame game(config);
    return game.run() ? 0 : 1;
}
