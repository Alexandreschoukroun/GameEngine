#include "scene/audio_sync.h"

#include "core/log.h"

#include <vector>

namespace scene {
namespace {

// Position d'une entite dans le monde : la quatrieme colonne de sa matrice.
core::Vec3 worldPosition(Scene& scene, Entity entity) {
    return core::Vec3(scene.worldMatrix(entity)[3]);
}

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

} // namespace scene
