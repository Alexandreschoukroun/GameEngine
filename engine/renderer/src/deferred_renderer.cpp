#include "renderer/deferred_renderer.h"

#include "assets/text_file.h"
#include "core/log.h"
#include "core/profiler.h"
#include "platform/paths.h"
#include "renderer/camera.h"
#include "rhi/mesh.h"
#include "rhi/texture.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <string>

namespace renderer {
namespace {

// Emplacements d'uniformes de la passe d'eclairage. Un tableau de 8 vec4 occupe 8
// emplacements consecutifs : d'ou le saut de 7 a 15.
// Passe de geometrie : 0 = viewProjection, 4 = modele, 8 = matrice des normales.
constexpr core::u32 kUniformNormalMapStrength = 12;
constexpr core::u32 kUniformBaseColorFactor = 13;
constexpr core::u32 kUniformMetallicFactor = 14;
constexpr core::u32 kUniformRoughnessFactor = 15;
// Passe d'eclairage.
constexpr core::u32 kUniformDebugView = 0;
constexpr core::u32 kUniformNearFar = 1;
constexpr core::u32 kUniformInverseViewProjection = 2;
constexpr core::u32 kUniformCameraPosition = 3;
constexpr core::u32 kUniformLightCount = 6;
constexpr core::u32 kUniformLightPositions = 7;
constexpr core::u32 kUniformLightColors = 15;
constexpr core::u32 kUniformLightDirections = 23;
constexpr core::u32 kUniformLightParams = 31;
constexpr core::u32 kUniformShadowViewProjection = 39; // une mat4 occupe 39 a 42
constexpr core::u32 kUniformShadowLightIndex = 43;
constexpr core::u32 kUniformHasCookie = 44;
constexpr core::u32 kUniformEnvironmentSky = 45;
constexpr core::u32 kUniformEnvironmentGround = 46;

// 1024 x 1024 en 24 bits : 3 Mo. Doubler la resolution quadruple la memoire.
constexpr core::u32 kShadowResolution = 1024;
constexpr core::u32 kShadowTextureUnit = 3;
constexpr core::u32 kCookieTextureUnit = 4;

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

} // namespace

bool DeferredRenderer::create(core::u32 width, core::u32 height) {
    if (!createProgramFromFiles(m_geometryProgram, "shaders/gbuffer.vert",
                                "shaders/gbuffer.frag") ||
        !createProgramFromFiles(m_lightingProgram, "shaders/present.vert",
                                "shaders/lighting.frag") ||
        !createProgramFromFiles(m_shadowProgram, "shaders/shadow.vert",
                                "shaders/shadow.frag")) {
        return false;
    }
    return m_gbuffer.create(width, height) && m_shadowMap.create(kShadowResolution);
}

void DeferredRenderer::destroy() {
    m_shadowMap.destroy();
    m_gbuffer.destroy();
    m_shadowProgram.destroy();
    m_lightingProgram.destroy();
    m_geometryProgram.destroy();
}

core::i32 DeferredRenderer::renderShadowPass(rhi::Device& device,
                                             std::span<const DrawItem> items,
                                             std::span<const Light> lights) {
    // Une seule lumiere a ombre pour l'instant : la premiere spot marquee comme telle.
    core::i32 shadowIndex = -1;
    for (core::u32 i = 0; i < lights.size() && i < kMaxLights; ++i) {
        if (lights[i].castsShadow && lights[i].type == LightType::Spot) {
            shadowIndex = static_cast<core::i32>(i);
            break;
        }
    }
    if (shadowIndex < 0) {
        return -1;
    }

    const Light& light = lights[static_cast<std::size_t>(shadowIndex)];

    // La lumiere devient une camera. Le champ de vision couvre tout le cone exterieur,
    // d'ou le facteur 2 : les angles du spot sont des demi-angles.
    const core::Vec3 direction = glm::normalize(light.direction);
    // lookAt a besoin d'un "haut" non colineaire a la direction, sinon la matrice degenere.
    const core::Vec3 up = std::abs(direction.y) > 0.99f ? core::Vec3{0.0f, 0.0f, 1.0f}
                                                        : core::Vec3{0.0f, 1.0f, 0.0f};
    const core::Mat4 view = glm::lookAt(light.position, light.position + direction, up);
    // Le plan proche ne peut pas etre trop petit : c'est lui qui fixe la precision utile
    // de la carte de profondeur.
    const core::Mat4 projection =
        glm::perspective(light.outerAngleRadians * 2.0f, 1.0f, 0.1f, light.range);
    m_shadowViewProjection = projection * view;

    ENGINE_PROFILE_SCOPE("shadow pass");
    device.bindShadowMap(m_shadowMap);
    m_shadowProgram.setMat4(0, m_shadowViewProjection);
    for (const DrawItem& item : items) {
        if (item.mesh != nullptr) {
            // Ni texture ni matiere : seule la geometrie compte pour mesurer une distance.
            // La transformation, elle, est indispensable : sans elle l'ombre resterait la
            // ou le fichier a laisse le modele.
            m_shadowProgram.setMat4(4, item.modelMatrix);
            device.draw(m_shadowProgram, *item.mesh);
        }
    }
    return shadowIndex;
}

bool DeferredRenderer::resize(core::u32 width, core::u32 height) {
    return m_gbuffer.create(width, height);
}

void DeferredRenderer::render(rhi::Device& device, const Camera& camera,
                              std::span<const DrawItem> items, std::span<const Light> lights,
                              core::u32 screenWidth, core::u32 screenHeight) {
    // Passe 0 : la scene vue depuis la lumiere, pour savoir ce qu'elle atteint.
    const core::i32 shadowLightIndex = renderShadowPass(device, items, lights);

    {
        // Passe 1 : chaque objet ecrit couleur, normale, rugosite et metallicite dans le
        // G-buffer. Aucun eclairage ici, donc aucun cout lie au nombre de lumieres.
        ENGINE_PROFILE_SCOPE("gbuffer pass");
        device.bindRenderTarget(m_gbuffer);
        device.clear(0.0f, 0.0f, 0.0f, 0.0f);
        m_geometryProgram.setMat4(0, camera.viewProjectionMatrix());

        for (const DrawItem& item : items) {
            if (item.mesh == nullptr || item.baseColor == nullptr ||
                item.metallicRoughness == nullptr) {
                continue;
            }
            // Ces deux uniformes changent a chaque objet, contrairement a la matrice de
            // camera qui vaut pour toute la passe.
            m_geometryProgram.setMat4(4, item.modelMatrix);
            m_geometryProgram.setMat3(8, item.normalMatrix);
            device.bindTexture(*item.baseColor, 0);
            device.bindTexture(*item.metallicRoughness, 1);

            // Un objet sans carte de normales garde sa normale geometrique. On coupe par
            // un uniforme plutot que par un second shader : deux programmes pour une
            // ligne de difference multiplieraient les changements d'etat GPU sans rien
            // faire gagner.
            const bool hasNormalMap = item.normalMap != nullptr;
            m_geometryProgram.setFloat(kUniformNormalMapStrength, hasNormalMap ? 1.0f : 0.0f);
            if (hasNormalMap) {
                device.bindTexture(*item.normalMap, 2);
            }

            m_geometryProgram.setVec4(kUniformBaseColorFactor, item.baseColorFactor);
            m_geometryProgram.setFloat(kUniformMetallicFactor, item.metallicFactor);
            m_geometryProgram.setFloat(kUniformRoughnessFactor, item.roughnessFactor);

            // indexCount nul : tout le maillage. C'est le cas des objets a matiere unique,
            // qui n'ont aucune raison de decrire une portion.
            if (item.indexCount == 0) {
                device.draw(m_geometryProgram, *item.mesh);
            } else {
                device.drawRange(m_geometryProgram, *item.mesh, item.firstIndex,
                                 item.indexCount);
            }
        }
    }

    {
        // Passe 2 : un triangle couvre l'ecran et calcule l'eclairage a partir du
        // G-buffer. Son cout depend du nombre de pixels et de lumieres, pas des objets.
        ENGINE_PROFILE_SCOPE("lighting pass");
        device.bindScreen(screenWidth, screenHeight);
        device.clear(0.0f, 0.0f, 0.0f, 1.0f);
        device.bindGBufferTexture(m_gbuffer, rhi::GBufferSlot::Albedo, 0);
        device.bindGBufferTexture(m_gbuffer, rhi::GBufferSlot::Normal, 1);
        device.bindGBufferTexture(m_gbuffer, rhi::GBufferSlot::Depth, 2);

        m_lightingProgram.setInt(kUniformDebugView, static_cast<core::i32>(m_debugView));
        m_lightingProgram.setVec2(kUniformNearFar, core::Vec2{camera.nearZ(), camera.farZ()});
        // L'inverse de la matrice de camera permet de retrouver la position du monde a
        // partir de la seule profondeur, donc de ne pas la stocker dans le G-buffer.
        m_lightingProgram.setMat4(kUniformInverseViewProjection,
                                  glm::inverse(camera.viewProjectionMatrix()));
        m_lightingProgram.setVec3(kUniformCameraPosition, camera.position());

        // Les lumieres sont recopiees dans deux tableaux compacts : le GPU recoit deux
        // envois, et non deux par lumiere.
        const core::u32 lightCount =
            static_cast<core::u32>(lights.size()) < kMaxLights
                ? static_cast<core::u32>(lights.size())
                : kMaxLights;
        std::array<core::Vec4, kMaxLights> positions{};
        std::array<core::Vec4, kMaxLights> colors{};
        std::array<core::Vec4, kMaxLights> directions{};
        std::array<core::Vec4, kMaxLights> params{};
        for (core::u32 i = 0; i < lightCount; ++i) {
            const Light& light = lights[i];
            positions[i] = core::Vec4(light.position, 0.0f);
            colors[i] = core::Vec4(light.color, light.intensity);
            // Les cosinus sont calcules ici plutot que dans le shader : c'est une fois par
            // lumiere et par frame, contre une fois par pixel.
            directions[i] =
                core::Vec4(glm::normalize(light.direction), std::cos(light.innerAngleRadians));
            params[i] = core::Vec4(std::cos(light.outerAngleRadians),
                                   static_cast<core::f32>(light.type), 0.0f, 0.0f);
        }

        m_lightingProgram.setInt(kUniformLightCount, static_cast<core::i32>(lightCount));
        m_lightingProgram.setVec4Array(kUniformLightPositions, positions.data(), kMaxLights);
        m_lightingProgram.setVec4Array(kUniformLightColors, colors.data(), kMaxLights);
        m_lightingProgram.setVec4Array(kUniformLightDirections, directions.data(), kMaxLights);
        m_lightingProgram.setVec4Array(kUniformLightParams, params.data(), kMaxLights);

        m_lightingProgram.setMat4(kUniformShadowViewProjection, m_shadowViewProjection);
        m_lightingProgram.setInt(kUniformShadowLightIndex, shadowLightIndex);
        device.bindShadowTexture(m_shadowMap, kShadowTextureUnit);

        // Le cookie se projette avec la meme matrice que l'ombre : la lumiere regarde sa
        // texture exactement comme elle regarde sa carte de profondeur.
        const bool hasCookie = m_spotCookie != nullptr && shadowLightIndex >= 0;
        m_lightingProgram.setInt(kUniformHasCookie, hasCookie ? 1 : 0);
        // L'intensite voyage dans le canal alpha du ciel : un uniforme de moins a poser,
        // et les deux informations changent toujours ensemble.
        m_lightingProgram.setVec4(kUniformEnvironmentSky,
                                  core::Vec4(m_environmentSky, m_environmentIntensity));
        m_lightingProgram.setVec4(kUniformEnvironmentGround,
                                  core::Vec4(m_environmentGround, 0.0f));
        if (hasCookie) {
            device.bindTexture(*m_spotCookie, kCookieTextureUnit);
        }

        device.drawFullscreenTriangle(m_lightingProgram);
    }
}

} // namespace renderer
