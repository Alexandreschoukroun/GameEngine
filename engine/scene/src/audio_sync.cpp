#include "scene/audio_sync.h"

#include "core/log.h"

#include <vector>

namespace scene {
namespace {

// Position d'une entite dans le monde : la quatrieme colonne de sa matrice.
core::Vec3 worldPosition(Scene& scene, Entity entity) {
    return core::Vec3(scene.worldMatrix(entity)[3]);
}

// Ecartement des rayons lateraux, en metres. Assez large pour qu'une porte entrouverte
// laisse passer un rayon sur trois, assez etroit pour ne pas traverser un mur voisin.
constexpr core::f32 kProbeSpread = 0.45f;

// Marge devant la source : sans elle, le rayon toucherait le collider de l'objet qui
// sonne lui-meme, et toute source posee sur une caisse se croirait murée.
constexpr core::f32 kSourceMargin = 0.15f;

} // namespace

void startAudioSources(Scene& scene, const ResourceTable& resources, audio::Engine& engine) {
    entt::registry& registry = scene.registry();

    // Les matrices monde doivent etre a jour : une source peut etre enfant d'autre chose,
    // et c'est sa position dans le monde qu'on entend.
    scene.updateWorldTransforms();

    // On collecte avant de creer : ajouter un composant pendant qu'on parcourt sa vue
    // invaliderait le parcours. Meme piege qu'a la creation des corps physiques.
    std::vector<Entity> pending;
    for (auto [entity, source] :
         registry.view<const AudioSource>(entt::exclude<AudioVoice>).each()) {
        (void)source;
        pending.push_back(entity);
    }

    for (Entity entity : pending) {
        const AudioSource& source = registry.get<const AudioSource>(entity);
        const audio::SoundHandle sound = resources.sound(source.sound);
        if (sound == audio::kInvalidSound) {
            // Une source sans son n'est pas une erreur fatale : le niveau se joue en
            // silence sur ce point, et l'avertissement du chargement l'a deja signale.
            continue;
        }

        const audio::VoiceHandle voice =
            engine.play(sound, worldPosition(scene, entity), source.looping, source.volume);
        if (voice == audio::kInvalidVoice) {
            continue;
        }
        registry.emplace<AudioVoice>(entity, AudioVoice{voice});
    }
}

void syncAudioSources(Scene& scene, audio::Engine& engine) {
    entt::registry& registry = scene.registry();
    for (auto [entity, voice] : registry.view<AudioVoice>().each()) {
        // Une voix terminee ne se replace pas : setVoicePosition ignore de toute facon les
        // identifiants perimes, mais l'eviter economise l'acces a la matrice monde.
        if (!engine.isVoicePlaying(voice.handle)) {
            continue;
        }
        engine.setVoicePosition(voice.handle, worldPosition(scene, entity));
    }
}

void updateAudioOcclusion(Scene& scene, const physics::World& world, audio::Engine& engine,
                          const core::Vec3& listenerPosition) {
    entt::registry& registry = scene.registry();

    for (auto [entity, voice] : registry.view<AudioVoice>().each()) {
        if (!engine.isVoicePlaying(voice.handle)) {
            continue;
        }

        const core::Vec3 source = worldPosition(scene, entity);
        const core::Vec3 toSource = source - listenerPosition;
        const core::f32 distance = glm::length(toSource);
        if (distance <= kSourceMargin) {
            // La source est dans l'oreille : rien ne peut s'interposer.
            engine.setVoiceOcclusion(voice.handle, 0.0f);
            continue;
        }

        const core::Vec3 direction = toSource / distance;
        // Un vecteur horizontal perpendiculaire au trajet : les obstacles d'un interieur
        // sont des murs et des portes, ils se contournent lateralement, pas par le haut.
        core::Vec3 side = glm::cross(direction, core::Vec3{0.0f, 1.0f, 0.0f});
        const core::f32 sideLength = glm::length(side);
        // Trajet vertical : aucune direction laterale ne se distingue, un seul rayon
        // suffit alors.
        side = sideLength > 0.001f ? side / sideLength : core::Vec3{0.0f, 0.0f, 0.0f};

        const core::Vec3 offsets[3] = {
            core::Vec3{0.0f, 0.0f, 0.0f},
            side * kProbeSpread,
            side * -kProbeSpread,
        };

        core::u32 blocked = 0;
        core::u32 cast = 0;
        for (const core::Vec3& offset : offsets) {
            const core::Vec3 origin = listenerPosition + offset;
            const core::Vec3 target = source + offset;
            const core::Vec3 ray = target - origin;
            const core::f32 rayLength = glm::length(ray);
            if (rayLength <= kSourceMargin) {
                continue;
            }
            ++cast;
            const physics::RayHit hit =
                world.raycast(origin, ray / rayLength, rayLength - kSourceMargin);
            if (hit.hit) {
                ++blocked;
            }
            // Le rayon central seul suffit quand les lateraux sont confondus avec lui.
            if (sideLength <= 0.001f) {
                break;
            }
        }

        const core::f32 occlusion =
            cast == 0 ? 0.0f
                      : static_cast<core::f32>(blocked) / static_cast<core::f32>(cast);
        engine.setVoiceOcclusion(voice.handle, occlusion);
    }
}

} // namespace scene
