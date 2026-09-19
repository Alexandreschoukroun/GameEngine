#include "assets/image.h"
#include "assets/mesh_data.h"
#include "core/math.h"
#include "core/types.h"
#include "platform/application.h"
#include "platform/paths.h"
#include "renderer/camera.h"
#include "renderer/deferred_renderer.h"
#include "renderer/flashlight.h"
#include "renderer/light.h"
#include "rhi/device.h"
#include "rhi/mesh.h"
#include "rhi/texture.h"
#include "scene/scene.h"

#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr const char* kModelPath = "models/suzanne/Suzanne.gltf";
constexpr const char* kBaseColorPath = "models/suzanne/Suzanne_BaseColor.png";
constexpr const char* kMetallicRoughnessPath = "models/suzanne/Suzanne_MetallicRoughness.png";

constexpr core::f32 kLookSensitivity = 0.0022f; // radians par pixel de souris
constexpr core::f32 kMoveSpeed = 3.0f;          // metres par seconde

// Piece fermee de 12 x 4 x 12 metres. Sans murs, le faisceau de la lampe partirait dans le
// vide et on ne verrait rien de son cone : le livrable du SPEC parle bien d'une PIECE
// eclairee par une lampe torche.
constexpr core::f32 kRoomHalfWidth = 6.0f;
constexpr core::f32 kRoomFloorY = -1.3f;
constexpr core::f32 kRoomCeilingY = 2.7f;
constexpr core::f32 kWallUvScale = 0.5f; // un carreau de damier par demi-metre

constexpr core::u32 kCheckerSize = 256;
constexpr core::u32 kCheckerSquare = 32;
constexpr core::u32 kCookieSize = 256;

// Nombre maximal de lumieres transmises au renderer en une frame : la torche plus celles
// de la scene.
constexpr std::size_t kMaxSceneLights = renderer::kMaxLights;

// Ajoute un quadrilatere a la piece. Les sommets sont donnes dans l'ordre anti-horaire vu
// depuis l'INTERIEUR : c'est de la que la camera regarde, et le culling eliminerait les
// faces prises a l'envers.
void addQuad(std::vector<rhi::Vertex>& vertices, std::vector<core::u32>& indices,
             const core::Vec3& a, const core::Vec3& b, const core::Vec3& c,
             const core::Vec3& d, const core::Vec3& normal, core::f32 uScale,
             core::f32 vScale) {
    const auto base = static_cast<core::u32>(vertices.size());
    const core::Vec3 n = normal;
    vertices.push_back({{a.x, a.y, a.z}, {n.x, n.y, n.z}, {0.0f, 0.0f}});
    vertices.push_back({{b.x, b.y, b.z}, {n.x, n.y, n.z}, {uScale, 0.0f}});
    vertices.push_back({{c.x, c.y, c.z}, {n.x, n.y, n.z}, {uScale, vScale}});
    vertices.push_back({{d.x, d.y, d.z}, {n.x, n.y, n.z}, {0.0f, vScale}});
    for (core::u32 index : {0u, 1u, 2u, 0u, 2u, 3u}) {
        indices.push_back(base + index);
    }
}

// Faisceau de lampe torche, en niveaux de gris : blanc = la lumiere passe, noir = elle est
// bloquee. Un disque parfait trahirait immediatement l'artifice, d'ou le point chaud
// decentre et les stries du reflecteur.
std::vector<core::u8> makeFlashlightCookie() {
    std::vector<core::u8> pixels(static_cast<std::size_t>(kCookieSize) * kCookieSize * 4);
    const core::f32 half = static_cast<core::f32>(kCookieSize) * 0.5f;

    for (core::u32 y = 0; y < kCookieSize; ++y) {
        for (core::u32 x = 0; x < kCookieSize; ++x) {
            // Centre du faisceau legerement decale : l'ampoule d'une vraie lampe n'est
            // jamais parfaitement alignee avec le reflecteur.
            const core::f32 dx = (static_cast<core::f32>(x) - half * 0.94f) / half;
            const core::f32 dy = (static_cast<core::f32>(y) - half * 1.04f) / half;
            const core::f32 distance = std::sqrt(dx * dx + dy * dy);

            // Bord doux : le faisceau s'eteint entre 0,55 et 1,0 du rayon.
            core::f32 intensity = 1.0f - glm::smoothstep(0.55f, 1.0f, distance);
            // Stries radiales du reflecteur, tres legeres.
            const core::f32 angle = std::atan2(dy, dx);
            intensity *= 0.90f + 0.10f * std::cos(angle * 7.0f);
            // Assombrissement general vers l'exterieur, pour un centre plus chaud.
            intensity *= 1.0f - 0.35f * distance;

            const auto value = static_cast<core::u8>(
                glm::clamp(intensity, 0.0f, 1.0f) * 255.0f);
            const std::size_t index = (static_cast<std::size_t>(y) * kCookieSize + x) * 4;
            pixels[index + 0] = value;
            pixels[index + 1] = value;
            pixels[index + 2] = value;
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

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
        m_flashlight.snapTo(m_camera);
        if (!loadModel() || !loadRoom()) {
            return false;
        }
        buildScene();
        return true;
    }

    // Une fois par frame : le regard suit la souris a la frequence de l'ecran.
    void onFrame(core::f64 frameDeltaSeconds) override {
        // Un deplacement souris est deja une quantite, pas un taux : il ne se multiplie
        // pas par le temps ecoule. Le rattrapage de la lampe, lui, en depend.
        m_camera.addRotation(input().mouseDeltaX() * kLookSensitivity,
                             -input().mouseDeltaY() * kLookSensitivity);
        m_flashlight.update(m_camera, frameDeltaSeconds);

        const bool fDown = input().isKeyDown(platform::Key::F);
        if (fDown && !m_fWasDown) {
            m_flashlight.toggle();
        }
        m_fWasDown = fDown;

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
        // Deux systemes, au sens ECS : ils parcourent les entites possedant les composants
        // qui les interessent, et en tirent ce que le renderer attend.
        m_drawItems.clear();
        for (auto [entity, transform, mesh] :
             m_scene.registry().view<scene::Transform, scene::MeshRenderer>().each()) {
            if (mesh.mesh == nullptr) {
                continue;
            }
            m_drawItems.push_back(renderer::DrawItem{mesh.mesh, mesh.baseColor,
                                                     mesh.metallicRoughness,
                                                     transform.matrix(),
                                                     transform.normalMatrix()});
        }

        // La lampe torche en premier : c'est elle qui porte l'ombre, et le renderer retient
        // la premiere lumiere a ombre de la liste.
        m_lights.clear();
        m_lights.push_back(m_flashlight.light());
        for (auto [entity, transform, source] :
             m_scene.registry().view<scene::Transform, scene::LightSource>().each()) {
            if (m_lights.size() >= kMaxSceneLights) {
                break;
            }
            renderer::Light light;
            // La position vient du Transform, jamais du composant lumiere : une seule
            // verite pour une seule information.
            light.position = transform.position;
            light.direction = core::Vec3(transform.matrix() * core::Vec4{0.0f, 0.0f, -1.0f, 0.0f});
            light.color = source.color;
            light.intensity = source.intensity;
            light.type = source.type;
            light.innerAngleRadians = source.innerAngleRadians;
            light.outerAngleRadians = source.outerAngleRadians;
            light.range = source.range;
            light.castsShadow = source.castsShadow;
            m_lights.push_back(light);
        }

        m_renderer.render(m_device, m_camera, m_drawItems, m_lights, window().width(),
                          window().height());
    }

    // Le contexte GPU est encore vivant ici : c'est le seul endroit ou liberer ces objets.
    void onShutdown() override {
        m_renderer.destroy();
        m_cookie.destroy();
        m_floorMaterial.destroy();
        m_floorBaseColor.destroy();
        m_floorMesh.destroy();
        m_modelMetallicRoughness.destroy();
        m_modelBaseColor.destroy();
        m_modelMesh.destroy();
    }

private:
    // La scene remplace les variables membres : chaque objet est une entite, decrite par
    // ses composants. C'est ce qui rendra le chargement depuis un fichier possible.
    void buildScene() {
        const scene::Entity room = m_scene.createEntity("piece");
        m_scene.registry().emplace<scene::MeshRenderer>(
            room, scene::MeshRenderer{&m_floorMesh, &m_floorBaseColor, &m_floorMaterial});

        // Deux exemplaires du meme maillage, a deux endroits et a deux echelles : c'est
        // exactement ce qui etait impossible avant, la geometrie etant figee a l'origine.
        const scene::Entity statue = m_scene.createEntity("statue");
        m_scene.registry().emplace<scene::MeshRenderer>(
            statue,
            scene::MeshRenderer{&m_modelMesh, &m_modelBaseColor, &m_modelMetallicRoughness});

        const scene::Entity statueLoin = m_scene.createEntity("statue lointaine");
        auto& farTransform = m_scene.registry().get<scene::Transform>(statueLoin);
        farTransform.position = core::Vec3{-3.2f, -0.4f, -3.6f};
        farTransform.rotation = core::Vec3{0.0f, core::radians(35.0f), 0.0f};
        farTransform.scale = core::Vec3{0.7f, 0.7f, 0.7f};
        m_scene.registry().emplace<scene::MeshRenderer>(
            statueLoin,
            scene::MeshRenderer{&m_modelMesh, &m_modelBaseColor, &m_modelMetallicRoughness});

        // La lumiere d'ambiance devient une entite comme les autres : sa position est dans
        // son Transform, pas dans son composant lumiere.
        const scene::Entity braise = m_scene.createEntity("braise");
        m_scene.registry().get<scene::Transform>(braise).position =
            core::Vec3{0.0f, 2.2f, -5.0f};
        m_scene.registry().emplace<scene::LightSource>(
            braise, scene::LightSource{core::Vec3{1.0f, 0.22f, 0.16f}, 3.0f});
    }

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

    bool loadRoom() {
        const core::f32 w = kRoomHalfWidth;
        const core::f32 floorY = kRoomFloorY;
        const core::f32 ceilY = kRoomCeilingY;
        const core::f32 uv = 2.0f * w * kWallUvScale;
        const core::f32 uvHeight = (ceilY - floorY) * kWallUvScale;

        std::vector<rhi::Vertex> vertices;
        std::vector<core::u32> indices;
        // Sol, plafond, puis les quatre murs. Toutes les normales pointent vers
        // l'interieur de la piece.
        addQuad(vertices, indices, {-w, floorY, -w}, {-w, floorY, w}, {w, floorY, w},
                {w, floorY, -w}, {0.0f, 1.0f, 0.0f}, uv, uv);
        addQuad(vertices, indices, {-w, ceilY, -w}, {w, ceilY, -w}, {w, ceilY, w},
                {-w, ceilY, w}, {0.0f, -1.0f, 0.0f}, uv, uv);
        addQuad(vertices, indices, {-w, floorY, -w}, {w, floorY, -w}, {w, ceilY, -w},
                {-w, ceilY, -w}, {0.0f, 0.0f, 1.0f}, uv, uvHeight);
        addQuad(vertices, indices, {w, floorY, w}, {-w, floorY, w}, {-w, ceilY, w},
                {w, ceilY, w}, {0.0f, 0.0f, -1.0f}, uv, uvHeight);
        addQuad(vertices, indices, {-w, floorY, w}, {-w, floorY, -w}, {-w, ceilY, -w},
                {-w, ceilY, w}, {1.0f, 0.0f, 0.0f}, uv, uvHeight);
        addQuad(vertices, indices, {w, floorY, -w}, {w, floorY, w}, {w, ceilY, w},
                {w, ceilY, -w}, {-1.0f, 0.0f, 0.0f}, uv, uvHeight);

        if (!m_floorMesh.create(vertices.data(), static_cast<core::u32>(vertices.size()),
                                indices.data(), static_cast<core::u32>(indices.size()))) {
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
        if (!m_floorMaterial.create(1, 1, material, rhi::TextureFormat::LinearData)) {
            return false;
        }

        // Le cookie module l'intensite de la lampe : ce sont des mesures, pas une couleur
        // a regarder, donc aucune conversion sRGB.
        const std::vector<core::u8> cookie = makeFlashlightCookie();
        if (!m_cookie.create(kCookieSize, kCookieSize, cookie.data(),
                             rhi::TextureFormat::LinearData)) {
            return false;
        }
        m_renderer.setSpotCookie(&m_cookie);
        return true;
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
    rhi::Texture m_cookie;

    renderer::Flashlight m_flashlight;
    scene::Scene m_scene;
    // Reutilises d'une frame a l'autre : on vide sans liberer, donc aucune allocation dans
    // la boucle de frame une fois le regime etabli (regle 7 du SPEC).
    std::vector<renderer::DrawItem> m_drawItems;
    std::vector<renderer::Light> m_lights;

    bool m_tabWasDown = false;
    bool m_fWasDown = false;
};

} // namespace

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M2";

    HorrorGame game(config);
    return game.run() ? 0 : 1;
}
