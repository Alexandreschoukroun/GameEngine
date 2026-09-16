#include "core/types.h"
#include "platform/application.h"
#include "rhi/device.h"
#include "rhi/mesh.h"
#include "rhi/shader_program.h"
#include "rhi/texture.h"

#include <iterator>
#include <vector>

namespace {

// Sommets ecrits directement en coordonnees normalisees (NDC) : X et Y vont de -1 a +1,
// quelle que soit la taille de la fenetre. Il n'y a pas encore de camera pour convertir
// des coordonnees du monde vers cet espace, ce sera l'etape 4.
// Les UV disent quel point de l'image correspond a chaque sommet ; le GPU interpole entre
// les trois pour donner sa coordonnee a chaque pixel.
constexpr rhi::Vertex kTriangle[] = {
    {{0.0f, 0.6f, 0.0f}, {0.5f, 1.0f}},
    {{-0.6f, -0.5f, 0.0f}, {0.0f, 0.0f}},
    {{0.6f, -0.5f, 0.0f}, {1.0f, 0.0f}},
};

constexpr core::u32 kCheckerSize = 256;
constexpr core::u32 kCheckerSquare = 32;

// Damier genere par le code plutot qu'un fichier image : aucune dependance de chargement
// n'est encore justifiee, et un damier rend immediatement visible la moindre erreur d'UV
// ou de deformation.
std::vector<core::u8> makeCheckerboard() {
    std::vector<core::u8> pixels(static_cast<std::size_t>(kCheckerSize) * kCheckerSize * 4);

    for (core::u32 y = 0; y < kCheckerSize; ++y) {
        for (core::u32 x = 0; x < kCheckerSize; ++x) {
            const bool light = ((x / kCheckerSquare) + (y / kCheckerSquare)) % 2 == 0;
            const core::u8 red = light ? 200 : 60;
            const core::u8 green = light ? 190 : 25;
            const core::u8 blue = light ? 175 : 30;

            const std::size_t index = (static_cast<std::size_t>(y) * kCheckerSize + x) * 4;
            pixels[index + 0] = red;
            pixels[index + 1] = green;
            pixels[index + 2] = blue;
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

// GLSL, compile par le pilote au demarrage du jeu. Ces sources partiront dans des fichiers
// quand la couche assets existera.
constexpr const char* kVertexShader = R"(#version 460 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 460 core
// binding = 0 : l'unite de texture sur laquelle le moteur branche l'image.
layout(binding = 0) uniform sampler2D uAlbedo;

in vec2 vTexCoord;
out vec4 outColor;

void main() {
    outColor = texture(uAlbedo, vTexCoord);
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
        if (!m_mesh.create(kTriangle, static_cast<core::u32>(std::size(kTriangle)))) {
            return false;
        }

        const std::vector<core::u8> pixels = makeCheckerboard();
        return m_texture.create(kCheckerSize, kCheckerSize, pixels.data());
    }

    void onRender() override {
        m_device.clear(0.04f, 0.0f, 0.02f, 1.0f);
        m_device.bindTexture(m_texture, 0);
        m_device.draw(m_program, m_mesh);
    }

    // Le contexte GPU est encore vivant ici : c'est le seul endroit ou liberer ces objets.
    void onShutdown() override {
        m_texture.destroy();
        m_mesh.destroy();
        m_program.destroy();
    }

private:
    rhi::Device m_device;
    rhi::ShaderProgram m_program;
    rhi::Mesh m_mesh;
    rhi::Texture m_texture;
};

} // namespace

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M1";

    HorrorGame game(config);
    return game.run() ? 0 : 1;
}
