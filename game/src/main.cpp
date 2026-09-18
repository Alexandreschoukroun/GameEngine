#include "core/math.h"
#include "core/types.h"
#include "platform/application.h"
#include "renderer/camera.h"
#include "rhi/device.h"
#include "rhi/mesh.h"
#include "rhi/shader_program.h"
#include "rhi/texture.h"

#include <array>
#include <iterator>
#include <vector>

namespace {

// Cube de 1 m de cote, centre sur l'origine du monde.
//
// 24 sommets et non 8 : chaque face a besoin de ses propres coordonnees de texture, donc
// un coin partage par trois faces porte trois UV differentes. En revanche les triangles
// sont decrits par des indices, ce qui evite les 36 sommets qu'il faudrait sans eux.
//
// L'ordre des sommets de chaque face est anti-horaire vu de l'exterieur : c'est ce qui
// permet au GPU de reconnaitre les faces avant et d'ignorer les autres.
constexpr rhi::Vertex kCubeVertices[] = {
    // face avant (+Z)
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}},
    // face arriere (-Z)
    {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f}},
    // face droite (+X)
    {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}},
    // face gauche (-X)
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f}},
    // face haute (+Y)
    {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f}},
    // face basse (-Y)
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}},
};

// Chaque face est un quadrilatere, donc deux triangles : 0-1-2 puis 0-2-3, decales de 4
// sommets a chaque face.
constexpr std::array<core::u32, 36> makeCubeIndices() {
    std::array<core::u32, 36> indices{};
    for (core::u32 face = 0; face < 6; ++face) {
        const core::u32 vertex = face * 4;
        const core::u32 index = face * 6;
        indices[index + 0] = vertex + 0;
        indices[index + 1] = vertex + 1;
        indices[index + 2] = vertex + 2;
        indices[index + 3] = vertex + 0;
        indices[index + 4] = vertex + 2;
        indices[index + 5] = vertex + 3;
    }
    return indices;
}

constexpr std::array<core::u32, 36> kCubeIndices = makeCubeIndices();

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

// location = 0 : l'emplacement d'uniforme que le moteur remplit avec la matrice de la
// camera. Il est declare ici, donc rien a chercher par son nom cote C++.
layout(location = 0) uniform mat4 uViewProjection;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    // Du monde vers l'espace clip : c'est cette multiplication qui remplace les
    // coordonnees ecrites a la main jusqu'ici.
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
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

// Sensibilite du regard, en radians par pixel de deplacement souris. Reglable par le
// joueur le jour ou il y aura des options (M8).
constexpr core::f32 kLookSensitivity = 0.0022f;
constexpr core::f32 kMoveSpeed = 3.0f; // metres par seconde

class HorrorGame final : public platform::Application {
public:
    using Application::Application;

protected:
    bool onInit() override {
        if (!m_device.create(window().glProcAddressLoader())) {
            return false;
        }
        m_device.setViewport(window().width(), window().height());

        const core::f32 aspect = static_cast<core::f32>(window().width()) /
                                 static_cast<core::f32>(window().height());
        m_camera.setPerspective(core::radians(60.0f), aspect, 0.05f, 100.0f);
        // Recule de 3 m : le cube est a l'origine, la camera le regarde depuis +Z.
        m_camera.setPosition(core::Vec3{0.0f, 0.0f, 3.0f});

        window().setRelativeMouseMode(true);

        if (!m_program.create(kVertexShader, kFragmentShader)) {
            return false;
        }
        if (!m_mesh.create(kCubeVertices, static_cast<core::u32>(std::size(kCubeVertices)),
                           kCubeIndices.data(),
                           static_cast<core::u32>(kCubeIndices.size()))) {
            return false;
        }

        const std::vector<core::u8> pixels = makeCheckerboard();
        return m_texture.create(kCheckerSize, kCheckerSize, pixels.data());
    }

    // Une fois par frame : le regard suit la souris a la frequence de l'ecran.
    void onFrame(core::f64 frameDeltaSeconds) override {
        (void)frameDeltaSeconds; // un deplacement souris est deja une quantite, pas un taux
        // Souris vers la droite (dx > 0) => on tourne vers +X, soit la droite de la vue
        // initiale. Souris vers le haut (dy < 0) => on leve les yeux, donc pitch positif.
        m_camera.addRotation(input().mouseDeltaX() * kLookSensitivity,
                             -input().mouseDeltaY() * kLookSensitivity);
    }

    // A pas fixe : le deplacement est de la simulation, il doit etre deterministe.
    void onFixedUpdate(core::f64 fixedDeltaSeconds) override {
        using platform::Key;

        core::Vec3 direction{0.0f, 0.0f, 0.0f};
        if (input().isKeyDown(Key::W)) {
            direction += m_camera.forward();
        }
        if (input().isKeyDown(Key::S)) {
            direction -= m_camera.forward();
        }
        if (input().isKeyDown(Key::D)) {
            direction += m_camera.right();
        }
        if (input().isKeyDown(Key::A)) {
            direction -= m_camera.right();
        }
        if (input().isKeyDown(Key::Space)) {
            direction.y += 1.0f;
        }
        if (input().isKeyDown(Key::LeftShift)) {
            direction.y -= 1.0f;
        }

        // Normaliser evite d'aller plus vite en diagonale. Le test protege glm::normalize,
        // qui divise par zero si le vecteur est nul.
        if (glm::dot(direction, direction) > 0.0f) {
            const core::f32 distance = kMoveSpeed * static_cast<core::f32>(fixedDeltaSeconds);
            m_camera.setPosition(m_camera.position() + glm::normalize(direction) * distance);
        }
    }

    void onResize(core::u32 width, core::u32 height) override {
        if (width == 0 || height == 0) {
            return; // fenetre reduite : on ignore, sinon on divise par zero
        }
        m_device.setViewport(width, height);
        m_camera.setAspect(static_cast<core::f32>(width) / static_cast<core::f32>(height));
    }

    void onRender() override {
        m_device.clear(0.04f, 0.0f, 0.02f, 1.0f);
        m_program.setMat4(0, m_camera.viewProjectionMatrix());
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
    renderer::Camera m_camera;
};

} // namespace

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M1";

    HorrorGame game(config);
    return game.run() ? 0 : 1;
}
