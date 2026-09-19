#include "renderer/deferred_renderer.h"

#include "assets/text_file.h"
#include "core/log.h"
#include "core/profiler.h"
#include "platform/paths.h"
#include "renderer/camera.h"
#include "rhi/mesh.h"
#include "rhi/texture.h"

#include <array>
#include <string>

namespace renderer {
namespace {

// Emplacements d'uniformes de la passe d'eclairage. Un tableau de 8 vec4 occupe 8
// emplacements consecutifs : d'ou le saut de 7 a 15.
constexpr core::u32 kUniformDebugView = 0;
constexpr core::u32 kUniformNearFar = 1;
constexpr core::u32 kUniformInverseViewProjection = 2;
constexpr core::u32 kUniformCameraPosition = 3;
constexpr core::u32 kUniformLightCount = 6;
constexpr core::u32 kUniformLightPositions = 7;
constexpr core::u32 kUniformLightColors = 15;

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
                                "shaders/lighting.frag")) {
        return false;
    }
    return m_gbuffer.create(width, height);
}

void DeferredRenderer::destroy() {
    m_gbuffer.destroy();
    m_lightingProgram.destroy();
    m_geometryProgram.destroy();
}

bool DeferredRenderer::resize(core::u32 width, core::u32 height) {
    return m_gbuffer.create(width, height);
}

void DeferredRenderer::render(rhi::Device& device, const Camera& camera,
                              std::span<const DrawItem> items, std::span<const Light> lights,
                              core::u32 screenWidth, core::u32 screenHeight) {
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
            device.bindTexture(*item.baseColor, 0);
            device.bindTexture(*item.metallicRoughness, 1);
            device.draw(m_geometryProgram, *item.mesh);
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
        for (core::u32 i = 0; i < lightCount; ++i) {
            positions[i] = core::Vec4(lights[i].position, 0.0f);
            colors[i] = core::Vec4(lights[i].color, lights[i].intensity);
        }

        m_lightingProgram.setInt(kUniformLightCount, static_cast<core::i32>(lightCount));
        m_lightingProgram.setVec4Array(kUniformLightPositions, positions.data(), kMaxLights);
        m_lightingProgram.setVec4Array(kUniformLightColors, colors.data(), kMaxLights);

        device.drawFullscreenTriangle(m_lightingProgram);
    }
}

} // namespace renderer
