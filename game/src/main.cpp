#include "assets/image.h"
#include "assets/mesh_data.h"
#include "core/math.h"
#include "core/types.h"
#include "platform/application.h"
#include "platform/paths.h"
#include "renderer/camera.h"
#include "renderer/deferred_renderer.h"
#include "renderer/light.h"
#include "rhi/device.h"
#include "rhi/mesh.h"
#include "rhi/texture.h"

#include <array>
#include <vector>

namespace {

constexpr const char* kModelPath = "models/suzanne/Suzanne.gltf";
constexpr const char* kBaseColorPath = "models/suzanne/Suzanne_BaseColor.png";
constexpr const char* kMetallicRoughnessPath = "models/suzanne/Suzanne_MetallicRoughness.png";

constexpr core::f32 kLookSensitivity = 0.0022f; // radians par pixel de souris
constexpr core::f32 kMoveSpeed = 3.0f;          // metres par seconde

constexpr core::f32 kFloorSize = 12.0f;
constexpr core::f32 kFloorHeight = -1.3f;
constexpr core::u32 kCheckerSize = 256;
constexpr core::u32 kCheckerSquare = 32;

// Sol : un quadrilatere horizontal, normale vers le haut, UV repetees pour que le damier
// se lise. C'est une surface DIELECTRIQUE, contrairement a Suzanne qui est un metal : elle
// rend enfin l'eclairage diffus lisible, et recevra les ombres a l'etape 5.
constexpr rhi::Vertex kFloorVertices[] = {
    {{-kFloorSize, kFloorHeight, -kFloorSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    {{kFloorSize, kFloorHeight, -kFloorSize}, {0.0f, 1.0f, 0.0f}, {8.0f, 0.0f}},
    {{kFloorSize, kFloorHeight, kFloorSize}, {0.0f, 1.0f, 0.0f}, {8.0f, 8.0f}},
    {{-kFloorSize, kFloorHeight, kFloorSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 8.0f}},
};
// Vu de dessus, l'ordre doit etre anti-horaire pour que la face soit consideree comme
// avant : sinon le sol serait invisible depuis le dessus.
constexpr core::u32 kFloorIndices[] = {0, 2, 1, 0, 3, 2};

// Trois lumieres pour rendre visible ce que le rendu differe permet : leur cout se paie
// par pixel d'ecran, pas par objet.
constexpr std::array<renderer::Light, 3> kLights = {
    renderer::Light{{1.8f, 1.6f, 2.2f}, {1.0f, 0.86f, 0.68f}, 26.0f}, // ampoule chaude
    renderer::Light{{-2.4f, 1.2f, 1.0f}, {0.40f, 0.55f, 1.0f}, 18.0f}, // appoint froid
    renderer::Light{{0.0f, -0.6f, -2.2f}, {1.0f, 0.25f, 0.18f}, 12.0f}, // contre-jour rouge
};

std::vector<core::u8> makeCheckerboard() {
    std::vector<core::u8> pixels(static_cast<std::size_t>(kCheckerSize) * kCheckerSize * 4);
    for (core::u32 y = 0; y < kCheckerSize; ++y) {
        for (core::u32 x = 0; x < kCheckerSize; ++x) {
            const bool light = ((x / kCheckerSquare) + (y / kCheckerSquare)) % 2 == 0;
            const std::size_t index = (static_cast<std::size_t>(y) * kCheckerSize + x) * 4;
            pixels[index + 0] = light ? 190 : 60;
            pixels[index + 1] = light ? 184 : 56;
            pixels[index + 2] = light ? 176 : 54;
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

// Les donnees du fichier sont neutres : c'est ici qu'elles prennent la forme attendue par
// le GPU. La couche assets ignore volontairement ce qu'est un sommet pour rhi.
std::vector<rhi::Vertex> toVertices(const assets::MeshData& meshData) {
    std::vector<rhi::Vertex> vertices(meshData.positions.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const core::Vec3& position = meshData.positions[i];
        const core::Vec3& normal = meshData.normals[i];
        const core::Vec2& uv = meshData.uvs[i];
        vertices[i] = rhi::Vertex{
            {position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, {uv.x, uv.y}};
    }
    return vertices;
}

class HorrorGame final : public platform::Application {
public:
    using Application::Application;

protected:
    bool onInit() override {
        if (!m_device.create(window().glProcAddressLoader())) {
            return false;
        }

        const core::f32 aspect = static_cast<core::f32>(window().width()) /
                                 static_cast<core::f32>(window().height());
        m_camera.setPerspective(core::radians(60.0f), aspect, 0.05f, 100.0f);
        m_camera.setPosition(core::Vec3{0.0f, 0.0f, 3.0f});
        window().setRelativeMouseMode(true);

        if (!m_renderer.create(window().width(), window().height())) {
            return false;
        }
        return loadModel() && loadFloor();
    }

    // Une fois par frame : le regard suit la souris a la frequence de l'ecran.
    void onFrame(core::f64 frameDeltaSeconds) override {
        (void)frameDeltaSeconds; // un deplacement souris est deja une quantite, pas un taux
        m_camera.addRotation(input().mouseDeltaX() * kLookSensitivity,
                             -input().mouseDeltaY() * kLookSensitivity);

        // Tab fait defiler les vues du G-buffer. On ne reagit qu'a l'instant ou la touche
        // s'enfonce : sinon la vue changerait soixante fois par seconde.
        const bool tabDown = input().isKeyDown(platform::Key::Tab);
        if (tabDown && !m_tabWasDown) {
            const auto next = (static_cast<core::i32>(m_renderer.debugView()) + 1) %
                              static_cast<core::i32>(renderer::DebugView::Count);
            m_renderer.setDebugView(static_cast<renderer::DebugView>(next));
        }
        m_tabWasDown = tabDown;
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
        m_camera.setAspect(static_cast<core::f32>(width) / static_cast<core::f32>(height));
        m_renderer.resize(width, height);
    }

    void onRender() override {
        // Le jeu decrit la scene ; le renderer decide comment la dessiner.
        const std::array<renderer::DrawItem, 2> items = {
            renderer::DrawItem{&m_modelMesh, &m_modelBaseColor, &m_modelMetallicRoughness},
            renderer::DrawItem{&m_floorMesh, &m_floorBaseColor, &m_floorMaterial},
        };
        m_renderer.render(m_device, m_camera, items, kLights, window().width(),
                          window().height());
    }

    // Le contexte GPU est encore vivant ici : c'est le seul endroit ou liberer ces objets.
    void onShutdown() override {
        m_renderer.destroy();
        m_floorMaterial.destroy();
        m_floorBaseColor.destroy();
        m_floorMesh.destroy();
        m_modelMetallicRoughness.destroy();
        m_modelBaseColor.destroy();
        m_modelMesh.destroy();
    }

private:
    bool loadModel() {
        assets::MeshData meshData;
        if (!assets::loadGltfMesh(platform::assetPath(kModelPath).c_str(), meshData)) {
            return false;
        }
        const std::vector<rhi::Vertex> vertices = toVertices(meshData);
        if (!m_modelMesh.create(vertices.data(), static_cast<core::u32>(vertices.size()),
                                meshData.indices.data(),
                                static_cast<core::u32>(meshData.indices.size()))) {
            return false;
        }

        assets::ImageData baseColor;
        assets::ImageData metallicRoughness;
        if (!assets::loadImage(platform::assetPath(kBaseColorPath).c_str(), baseColor) ||
            !assets::loadImage(platform::assetPath(kMetallicRoughnessPath).c_str(),
                               metallicRoughness)) {
            return false;
        }

        // La couleur de base est une couleur : le GPU doit la ramener en lineaire a chaque
        // lecture. La carte metallicite/rugosite contient des mesures : aucune conversion,
        // sinon les valeurs seraient faussees.
        return m_modelBaseColor.create(baseColor.width, baseColor.height,
                                       baseColor.pixels.data(),
                                       rhi::TextureFormat::SrgbColor) &&
               m_modelMetallicRoughness.create(metallicRoughness.width,
                                               metallicRoughness.height,
                                               metallicRoughness.pixels.data(),
                                               rhi::TextureFormat::LinearData);
    }

    bool loadFloor() {
        if (!m_floorMesh.create(kFloorVertices,
                                static_cast<core::u32>(std::size(kFloorVertices)),
                                kFloorIndices,
                                static_cast<core::u32>(std::size(kFloorIndices)))) {
            return false;
        }

        const std::vector<core::u8> pixels = makeCheckerboard();
        if (!m_floorBaseColor.create(kCheckerSize, kCheckerSize, pixels.data(),
                                     rhi::TextureFormat::SrgbColor)) {
            return false;
        }

        // Materiau constant en une texture de 1x1 pixel : convention glTF, le vert porte
        // la rugosite et le bleu la metallicite. Le shader ne fait aucune difference avec
        // une vraie carte, et on evite d'ajouter des parametres de matiere partout.
        const core::u8 material[4] = {0, 200, 0, 255}; // rugueux, non metallique
        return m_floorMaterial.create(1, 1, material, rhi::TextureFormat::LinearData);
    }

    rhi::Device m_device;
    renderer::DeferredRenderer m_renderer;
    renderer::Camera m_camera;

    rhi::Mesh m_modelMesh;
    rhi::Texture m_modelBaseColor;
    rhi::Texture m_modelMetallicRoughness;

    rhi::Mesh m_floorMesh;
    rhi::Texture m_floorBaseColor;
    rhi::Texture m_floorMaterial;

    bool m_tabWasDown = false;
};

} // namespace

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M2";

    HorrorGame game(config);
    return game.run() ? 0 : 1;
}
