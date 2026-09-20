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
    // Facultative : sans elle, la surface est eclairee par sa seule normale geometrique.
    // Elle n'a d'effet que si le maillage porte des tangentes.
    const rhi::Texture* normalMap = nullptr;

    // Portion du maillage a dessiner. indexCount = 0 signifie "tout le maillage", ce qui
    // laisse les objets a matiere unique s'ecrire sans rien preciser.
    core::u32 firstIndex = 0;
    core::u32 indexCount = 0;

    // Facteurs du materiau, tels que glTF les definit : ils MULTIPLIENT les textures.
    // Un materiau sans carte de couleur mais avec un facteur rouge donne un objet rouge
    // uni - c'est ainsi que sont faits la plupart des modeles simples.
    core::Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    core::f32 metallicFactor = 1.0f;
    core::f32 roughnessFactor = 1.0f;
    // Place l'objet dans le monde. Sans elle, toute la geometrie resterait la ou le
    // fichier l'a laissee, et deux exemplaires du meme modele se superposeraient.
    core::Mat4 modelMatrix{1.0f};
    // Transposee de l'inverse de la partie rotation/echelle : une normale ne se transforme
    // pas comme un point.
    core::Mat3 normalMatrix{1.0f};
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

    // Lumiere que l'environnement renvoie : ce que les murs, le sol et le plafond se
    // reflechissent entre eux. Sans elle, tout metal est noir - un metal n'a pas de
    // composante diffuse, il ne fait que reflechir.
    void setEnvironment(const core::Vec3& skyColor, const core::Vec3& groundColor,
                        core::f32 intensity) {
        m_environmentSky = skyColor;
        m_environmentGround = groundColor;
        m_environmentIntensity = intensity;
    }

    void setDebugView(DebugView view) { m_debugView = view; }
    DebugView debugView() const { return m_debugView; }

    // Texture projetee par la lumiere a ombre, qui module son faisceau. Le terme vient du
    // cinema : un cookie est un cache decoupe place devant un projecteur. Sans elle, le
    // cone est un disque parfait, ce qu'aucune vraie lampe ne produit.
    // nullptr pour n'en projeter aucune.
    void setSpotCookie(const rhi::Texture* cookie) { m_spotCookie = cookie; }

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
    const rhi::Texture* m_spotCookie = nullptr;
    DebugView m_debugView = DebugView::Lit;
    core::Vec3 m_environmentSky{0.05f, 0.055f, 0.07f};
    core::Vec3 m_environmentGround{0.02f, 0.018f, 0.015f};
    core::f32 m_environmentIntensity = 1.0f;
};

} // namespace renderer
