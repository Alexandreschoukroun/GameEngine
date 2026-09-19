#include "scene/footsteps.h"

#include "scene/physics_sync.h"

namespace scene {
namespace {

// Hauteur de depart du rayon, au-dessus des pieds, et sa portee. Partir exactement des
// pieds risquerait de demarrer DANS le sol, ou le rayon ne toucherait rien.
constexpr core::f32 kProbeLift = 0.3f;
constexpr core::f32 kProbeLength = 0.9f;

// Variation appliquee a chaque pas. Sans elle, cinquante pas identiques trahissent la
// machine : l'oreille repere une repetition exacte bien mieux qu'une difference.
constexpr core::f32 kPitchSpread = 0.08f;  // +/- 8 % de hauteur
constexpr core::f32 kVolumeSpread = 0.15f; // +/- 15 % de volume

// xorshift : trois decalages, aucune allocation, une suite parfaitement reproductible.
// Un pas n'a pas besoin de meilleur hasard que ca.
core::f32 nextUnit(core::u32& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<core::f32>(state & 0xFFFFFFu) / static_cast<core::f32>(0xFFFFFF);
}

} // namespace

Entity entityForBody(const Scene& scene, physics::BodyHandle body) {
    if (body == physics::kInvalidBody) {
        return kInvalidEntity;
    }
    for (auto [entity, physicsBody] : scene.registry().view<const PhysicsBody>().each()) {
        if (physicsBody.handle == body) {
            return entity;
        }
    }
    return kInvalidEntity;
}

void FootstepPlayer::configure(core::f32 strideLength, core::f32 volume) {
    // Une foulee nulle declencherait un pas par image.
    m_strideLength = strideLength > 0.01f ? strideLength : 0.01f;
    m_volume = volume;
}

ResourceHandle FootstepPlayer::update(const Scene& scene, const physics::World& world,
                                      const ResourceTable& resources, audio::Engine& engine,
                                      const core::Vec3& feetPosition, bool onGround,
                                      core::f32 travelledDistance) {
    if (!onGround) {
        // En l'air, on ne marche pas. Le compteur repart de zero pour qu'un atterrissage
        // ne declenche pas un demi-pas immediat.
        m_accumulated = 0.0f;
        return kInvalidResource;
    }

    m_accumulated += travelledDistance;
    if (m_accumulated < m_strideLength) {
        return kInvalidResource;
    }
    m_accumulated -= m_strideLength;
    // Un deplacement brutal - une telecommande de debug, un futur teleportement - ne doit
    // pas produire une rafale de pas pour rattraper la distance.
    if (m_accumulated > m_strideLength) {
        m_accumulated = 0.0f;
    }

    // Ce qu'on foule : un rayon vers le bas, depuis juste au-dessus des pieds.
    const physics::RayHit hit =
        world.raycast(feetPosition + core::Vec3{0.0f, kProbeLift, 0.0f},
                      core::Vec3{0.0f, -1.0f, 0.0f}, kProbeLength);
    if (!hit.hit) {
        return kInvalidResource;
    }

    const Entity ground = entityForBody(scene, hit.body);
    if (ground == kInvalidEntity) {
        return kInvalidResource;
    }

    const Surface* surface = scene.registry().try_get<Surface>(ground);
    if (surface == nullptr) {
        // Sol sans matiere declaree : silence. Un son de pas arbitraire serait pire, il
        // dirait au joueur une matiere que le niveau n'a pas decrite.
        return kInvalidResource;
    }

    const audio::SoundHandle sound = resources.sound(surface->footstep);
    if (sound == audio::kInvalidSound) {
        return kInvalidResource;
    }

    const core::f32 pitch = 1.0f + (nextUnit(m_noise) * 2.0f - 1.0f) * kPitchSpread;
    const core::f32 volume = m_volume * (1.0f + (nextUnit(m_noise) * 2.0f - 1.0f) * kVolumeSpread);
    // Le pas sort du POINT DE CONTACT, pas du centre du joueur : c'est ce qui le place au
    // sol plutot qu'a hauteur d'oreille.
    engine.play(sound, hit.point, false, volume, pitch);
    return surface->footstep;
}

} // namespace scene
