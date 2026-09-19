#pragma once

#include "core/types.h"
#include "renderer/light.h"
#include "rhi/device.h"
#include "rhi/render_target.h"
#include "rhi/shader_program.h"
#include "rhi/shadow_map.h"

#include <span>

namespace rhi {
class Mesh;
class Texture;
} // namespace rhi

namespace renderer {

class Camera;

// Ce qu'il faut pour dessiner un objet : sa geometrie et ses deux textures de matiere.
// C'est l'ancetre de ce que la scene de M3 produira automatiquement a partir des entites.
struct DrawItem {
    const rhi::Mesh* mesh = nullptr;
    const rhi::Texture* baseColor = nullptr;
    const rhi::Texture* metallicRoughness = nullptr;
};

// Couches du G-buffer affichables telles quelles, pour verifier de ses yeux ce que la
// passe de geometrie a ecrit.
enum class DebugView : core::i32 {
    Lit = 0,
    Albedo = 1,
    Normal = 2,
    Depth = 3,
    Count = 4,
};

// Rendu differe : la geometrie ecrit ses proprietes de surface dans un G-buffer, puis une
// passe plein ecran calcule l'eclairage. Le jeu decrit QUOI dessiner ; cette classe decide
// COMMENT - passes, cibles, uniformes, ordre des operations.
class DeferredRenderer {
public:
    DeferredRenderer() = default;

    DeferredRenderer(const DeferredRenderer&) = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;

    // Charge les shaders et alloue le G-buffer a la taille donnee.
    bool create(core::u32 width, core::u32 height);

    // Meme regle que dans rhi : a appeler tant que le contexte GPU est vivant.
    void destroy();

    // Le G-buffer a la taille de l'ecran : une texture ne se redimensionne pas, il faut
    // la recreer.
    bool resize(core::u32 width, core::u32 height);

    void setDebugView(DebugView view) { m_debugView = view; }
    DebugView debugView() const { return m_debugView; }

    // Les lumieres au-dela de kMaxLights sont ignorees.
    void render(rhi::Device& device, const Camera& camera, std::span<const DrawItem> items,
                std::span<const Light> lights, core::u32 screenWidth,
                core::u32 screenHeight);

private:
    // Rend la scene depuis la lumiere a ombre, si la liste en contient une. Renvoie son
    // indice, ou -1 s'il n'y en a pas.
    core::i32 renderShadowPass(rhi::Device& device, std::span<const DrawItem> items,
                               std::span<const Light> lights);

    rhi::RenderTarget m_gbuffer;
    rhi::ShadowMap m_shadowMap;
    rhi::ShaderProgram m_geometryProgram;
    rhi::ShaderProgram m_lightingProgram;
    rhi::ShaderProgram m_shadowProgram;
    core::Mat4 m_shadowViewProjection{1.0f};
    DebugView m_debugView = DebugView::Lit;
};

} // namespace renderer
