#include "assets/image.h"
#include "assets/mesh_data.h"
#include "assets/text_file.h"
#include "core/math.h"
#include "core/types.h"
#include "platform/application.h"
#include "platform/paths.h"
#include "renderer/camera.h"
#include "rhi/device.h"
#include "rhi/mesh.h"
#include "rhi/render_target.h"
#include "rhi/shader_program.h"
#include "rhi/texture.h"

#include <string>
#include <vector>

namespace {

constexpr const char* kModelPath = "models/suzanne/Suzanne.gltf";
constexpr const char* kBaseColorPath = "models/suzanne/Suzanne_BaseColor.png";
constexpr const char* kGBufferVertexPath = "shaders/gbuffer.vert";
constexpr const char* kGBufferFragmentPath = "shaders/gbuffer.frag";
constexpr const char* kLightingVertexPath = "shaders/present.vert";
constexpr const char* kLightingFragmentPath = "shaders/lighting.frag";
constexpr const char* kMetallicRoughnessPath = "models/suzanne/Suzanne_MetallicRoughness.png";

// Vues parcourues avec Tab : image eclairee, puis les trois couches brutes du G-buffer.
constexpr core::i32 kViewCount = 4;

// Une seule lumiere pour l'instant, posee a cote de la camera de depart. La lampe torche
// et les lumieres multiples viendront aux etapes suivantes.
constexpr core::Vec3 kLightPosition{1.6f, 1.4f, 2.2f};
// Couleur chaude, legerement ambree : une ampoule, pas un neon. La puissance compense la
// decroissance en 1/d^2, qui divise deja par 9 a 3 metres.
constexpr core::Vec4 kLightColorIntensity{1.0f, 0.88f, 0.72f, 24.0f};

// Sensibilite du regard, en radians par pixel de deplacement souris. Reglable par le
// joueur le jour ou il y aura des options (M8).
constexpr core::f32 kLookSensitivity = 0.0022f;
constexpr core::f32 kMoveSpeed = 3.0f; // metres par seconde

// Les shaders sont des donnees du jeu, au meme titre qu'une texture : ils vivent dans
// assets/shaders/ et se modifient sans recompiler le C++.
bool createProgramFromFiles(rhi::ShaderProgram& program, const char* vertexPath,
                            const char* fragmentPath) {
    std::string vertexSource;
    std::string fragmentSource;
    if (!assets::loadTextFile(platform::assetPath(vertexPath).c_str(), vertexSource) ||
        !assets::loadTextFile(platform::assetPath(fragmentPath).c_str(), fragmentSource)) {
        return false;
    }
    return program.create(vertexSource, fragmentSource);
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
        m_device.setViewport(window().width(), window().height());

        const core::f32 aspect = static_cast<core::f32>(window().width()) /
                                 static_cast<core::f32>(window().height());
        m_camera.setPerspective(core::radians(60.0f), aspect, 0.05f, 100.0f);
        // Recule de 3 m : le modele est a l'origine, la camera le regarde depuis +Z.
        m_camera.setPosition(core::Vec3{0.0f, 0.0f, 3.0f});

        window().setRelativeMouseMode(true);

        if (!createProgramFromFiles(m_gbufferProgram, kGBufferVertexPath,
                                    kGBufferFragmentPath) ||
            !createProgramFromFiles(m_lightingProgram, kLightingVertexPath,
                                    kLightingFragmentPath)) {
            return false;
        }

        if (!m_gbuffer.create(window().width(), window().height())) {
            return false;
        }

        assets::MeshData meshData;
        if (!assets::loadGltfMesh(platform::assetPath(kModelPath).c_str(), meshData)) {
            return false;
        }
        const std::vector<rhi::Vertex> vertices = toVertices(meshData);
        if (!m_mesh.create(vertices.data(), static_cast<core::u32>(vertices.size()),
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
        return m_baseColor.create(baseColor.width, baseColor.height, baseColor.pixels.data(),
                                  rhi::TextureFormat::SrgbColor) &&
               m_metallicRoughness.create(metallicRoughness.width, metallicRoughness.height,
                                          metallicRoughness.pixels.data(),
                                          rhi::TextureFormat::LinearData);
    }

    // Une fois par frame : le regard suit la souris a la frequence de l'ecran.
    void onFrame(core::f64 frameDeltaSeconds) override {
        (void)frameDeltaSeconds; // un deplacement souris est deja une quantite, pas un taux
        // Souris vers la droite (dx > 0) => on tourne vers +X, soit la droite de la vue
        // initiale. Souris vers le haut (dy < 0) => on leve les yeux, donc pitch positif.
        m_camera.addRotation(input().mouseDeltaX() * kLookSensitivity,
                             -input().mouseDeltaY() * kLookSensitivity);

        // Tab fait defiler les vues du G-buffer. On ne reagit qu'a l'instant ou la touche
        // s'enfonce : sinon la vue changerait soixante fois par seconde tant qu'on appuie.
        const bool tabDown = input().isKeyDown(platform::Key::Tab);
        if (tabDown && !m_tabWasDown) {
            m_view = (m_view + 1) % kViewCount;
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
        m_device.setViewport(width, height);
        m_camera.setAspect(static_cast<core::f32>(width) / static_cast<core::f32>(height));
        // Le G-buffer a la taille de l'ecran : il faut le recreer a chaque
        // redimensionnement, une texture ne se redimensionne pas.
        m_gbuffer.create(width, height);
    }

    void onRender() override {
        // Passe 1 : la geometrie ecrit ses proprietes de surface dans le G-buffer.
        // Aucun eclairage ici.
        m_device.bindRenderTarget(m_gbuffer);
        m_device.clear(0.0f, 0.0f, 0.0f, 0.0f);
        m_gbufferProgram.setMat4(0, m_camera.viewProjectionMatrix());
        m_device.bindTexture(m_baseColor, 0);
        m_device.bindTexture(m_metallicRoughness, 1);
        m_device.draw(m_gbufferProgram, m_mesh);

        // Passe 2 : un seul triangle couvre l'ecran, relit le G-buffer et calcule la
        // lumiere. Son cout ne depend pas du nombre d'objets de la scene.
        m_device.bindScreen(window().width(), window().height());
        m_device.clear(0.0f, 0.0f, 0.0f, 1.0f);
        m_device.bindGBufferTexture(m_gbuffer, rhi::GBufferSlot::Albedo, 0);
        m_device.bindGBufferTexture(m_gbuffer, rhi::GBufferSlot::Normal, 1);
        m_device.bindGBufferTexture(m_gbuffer, rhi::GBufferSlot::Depth, 2);
        m_lightingProgram.setInt(0, m_view);
        m_lightingProgram.setVec2(1, core::Vec2{m_camera.nearZ(), m_camera.farZ()});
        // L'inverse de la matrice de camera permet de retrouver la position du monde a
        // partir de la seule profondeur, donc de ne pas la stocker dans le G-buffer.
        m_lightingProgram.setMat4(2, glm::inverse(m_camera.viewProjectionMatrix()));
        m_lightingProgram.setVec3(3, m_camera.position());
        m_lightingProgram.setVec3(4, kLightPosition);
        m_lightingProgram.setVec4(5, kLightColorIntensity);
        m_device.drawFullscreenTriangle(m_lightingProgram);
    }

    // Le contexte GPU est encore vivant ici : c'est le seul endroit ou liberer ces objets.
    void onShutdown() override {
        m_gbuffer.destroy();
        m_metallicRoughness.destroy();
        m_baseColor.destroy();
        m_mesh.destroy();
        m_lightingProgram.destroy();
        m_gbufferProgram.destroy();
    }

private:
    rhi::Device m_device;
    rhi::ShaderProgram m_gbufferProgram;
    rhi::ShaderProgram m_lightingProgram;
    rhi::Mesh m_mesh;
    rhi::Texture m_baseColor;
    rhi::Texture m_metallicRoughness;
    rhi::RenderTarget m_gbuffer;
    renderer::Camera m_camera;
    core::i32 m_view = 0;
    bool m_tabWasDown = false;
};

} // namespace

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M2";

    HorrorGame game(config);
    return game.run() ? 0 : 1;
}
