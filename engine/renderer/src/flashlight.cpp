#include "renderer/flashlight.h"

#include "renderer/camera.h"

#include <cmath>

namespace renderer {
namespace {

// Decalage par rapport a l'oeil : la lampe est tenue dans la main droite, un peu plus bas.
// C'est ce qui fait que les ombres projetees ne sont pas exactement derriere les objets.
constexpr core::f32 kRightOffset = 0.22f;
constexpr core::f32 kDownOffset = 0.18f;

// Vitesses de rattrapage, en unites par seconde. La main suit vite, le poignet tourne
// plus lentement : le faisceau balaie donc legerement en retard sur le regard.
constexpr core::f32 kPositionCatchUp = 24.0f;
constexpr core::f32 kDirectionCatchUp = 11.0f;

// Caracteristiques du faisceau. Cone assez serre : une torche, pas un plafonnier.
//
// L'intensite parait enorme parce que l'attenuation suit 1/d^2 : a 8 metres, elle divise
// deja par 64. Pour qu'un mur du fond reste lisible, il faut compenser.
constexpr core::f32 kIntensity = 160.0f;
constexpr core::f32 kRange = 25.0f;

// Lissage exponentiel independant du framerate.
//
// Le piege classique est d'ecrire "valeur += (cible - valeur) * 0.1" a chaque frame : le
// rattrapage est alors deux fois plus rapide a 120 fps qu'a 60. Passer par l'exponentielle
// du temps ecoule donne le meme comportement quel que soit le framerate.
core::f32 smoothingFactor(core::f32 catchUpRate, core::f32 deltaSeconds) {
    return 1.0f - std::exp(-catchUpRate * deltaSeconds);
}

core::Vec3 desiredPosition(const Camera& camera) {
    const core::Vec3 up = glm::normalize(glm::cross(camera.right(), camera.forward()));
    return camera.position() + camera.right() * kRightOffset - up * kDownOffset;
}

} // namespace

void Flashlight::update(const Camera& camera, core::f64 frameDeltaSeconds) {
    const auto delta = static_cast<core::f32>(frameDeltaSeconds);
    if (delta <= 0.0f) {
        return;
    }

    m_position = glm::mix(m_position, desiredPosition(camera),
                          smoothingFactor(kPositionCatchUp, delta));
    // On interpole la direction puis on renormalise : entre deux directions proches,
    // c'est indiscernable d'une interpolation spherique, et bien moins cher.
    m_direction = glm::normalize(
        glm::mix(m_direction, camera.forward(), smoothingFactor(kDirectionCatchUp, delta)));
}

void Flashlight::snapTo(const Camera& camera) {
    m_position = desiredPosition(camera);
    m_direction = camera.forward();
}

Light Flashlight::light() const {
    Light light;
    light.position = m_position;
    light.direction = m_direction;
    // Blanc legerement chaud, comme une ampoule a incandescence.
    light.color = core::Vec3{1.0f, 0.95f, 0.86f};
    light.intensity = m_enabled ? kIntensity : 0.0f;
    light.type = LightType::Spot;
    light.innerAngleRadians = core::radians(13.0f);
    light.outerAngleRadians = core::radians(24.0f);
    light.range = kRange;
    light.castsShadow = true;
    return light;
}

} // namespace renderer
