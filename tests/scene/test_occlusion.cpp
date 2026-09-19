#include <doctest/doctest.h>

#include "audio/engine.h"
#include "physics/world.h"
#include "platform/paths.h"
#include "scene/audio_sync.h"
#include "scene/components.h"
#include "scene/resource_table.h"
#include "scene/scene.h"

namespace {

// Un monde ou une source sonne a un endroit, et une oreille qui ecoute a un autre.
struct World {
    audio::Engine audio;
    physics::World physics;
    scene::Scene scene;
    scene::ResourceTable resources;
    scene::Entity source = scene::kInvalidEntity;
    audio::VoiceHandle voice = audio::kInvalidVoice;

    bool build(const core::Vec3& sourcePosition) {
        if (!audio.create(true) || !physics.create()) {
            return false;
        }
        const audio::SoundHandle sound =
            audio.loadSound(platform::assetPath("audio/braises.wav"));
        if (sound == audio::kInvalidSound) {
            return false;
        }
        const scene::ResourceHandle handle = resources.addSound("braises", sound);

        source = scene.createEntity("source");
        scene.registry().get<scene::Transform>(source).position = sourcePosition;
        scene.registry().emplace<scene::AudioSource>(
            source, scene::AudioSource{handle, 1.0f, true});

        scene::startAudioSources(scene, resources, audio);
        const auto* started = scene.registry().try_get<scene::AudioVoice>(source);
        if (started == nullptr) {
            return false;
        }
        voice = started->handle;
        return true;
    }

    // L'occlusion est lissee dans le temps : on laisse passer assez de frames pour
    // qu'elle rejoigne sa consigne avant de mesurer.
    void settle() {
        for (core::u32 i = 0; i < 60; ++i) {
            audio.update(1.0f / 60.0f);
        }
    }
};

constexpr core::Vec3 kListener{0.0f, 0.0f, 0.0f};
constexpr core::Vec3 kSource{0.0f, 0.0f, -6.0f};

} // namespace

TEST_CASE("A clear line of sight leaves the sound untouched") {
    World w;
    REQUIRE(w.build(kSource));

    scene::updateAudioOcclusion(w.scene, w.physics, w.audio, kListener);
    w.settle();

    CHECK(w.audio.voiceOcclusion(w.voice) == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("A wall between the ear and the source occludes it fully") {
    World w;
    REQUIRE(w.build(kSource));

    // Un mur large, en travers du trajet : les trois rayons le rencontrent.
    w.physics.addBox(core::Vec3{0.0f, 0.0f, -3.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{4.0f, 2.0f, 0.1f}, true);

    scene::updateAudioOcclusion(w.scene, w.physics, w.audio, kListener);
    w.settle();

    CHECK(w.audio.voiceOcclusion(w.voice) == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("A narrow obstacle occludes only part of the sound") {
    World w;
    REQUIRE(w.build(kSource));

    // Un obstacle etroit, centre sur le trajet direct : il arrete le rayon du milieu, les
    // deux lateraux passent a cote. C'est le cas d'une porte entrouverte, et c'est ce que
    // trois rayons permettent d'exprimer la ou un seul donnerait tout ou rien.
    w.physics.addBox(core::Vec3{0.0f, 0.0f, -3.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.2f, 2.0f, 0.1f}, true);

    scene::updateAudioOcclusion(w.scene, w.physics, w.audio, kListener);
    w.settle();

    const core::f32 occlusion = w.audio.voiceOcclusion(w.voice);
    CHECK(occlusion > 0.2f);
    CHECK(occlusion < 0.8f);
}

TEST_CASE("Occlusion reaches its target progressively, not in one frame") {
    World w;
    REQUIRE(w.build(kSource));
    w.physics.addBox(core::Vec3{0.0f, 0.0f, -3.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{4.0f, 2.0f, 0.1f}, true);

    // Premiere application : la consigne est prise telle quelle, sinon on entendrait une
    // porte s'ouvrir au lancement de chaque niveau.
    scene::updateAudioOcclusion(w.scene, w.physics, w.audio, kListener);
    w.audio.update(1.0f / 60.0f);
    CHECK(w.audio.voiceOcclusion(w.voice) == doctest::Approx(1.0f).epsilon(0.01));

    // Le mur disparait : le retour au degage, lui, doit etre progressif. Un rayon qui
    // clignote entre deux frames produirait sinon un cliquetis audible.
    w.physics.destroy();
    REQUIRE(w.physics.create());
    scene::updateAudioOcclusion(w.scene, w.physics, w.audio, kListener);
    w.audio.update(1.0f / 60.0f);

    const core::f32 afterOneFrame = w.audio.voiceOcclusion(w.voice);
    CHECK(afterOneFrame < 1.0f);
    CHECK(afterOneFrame > 0.5f);

    w.settle();
    CHECK(w.audio.voiceOcclusion(w.voice) == doctest::Approx(0.0f).epsilon(0.01));
}
