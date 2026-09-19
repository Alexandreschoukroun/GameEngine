#include <doctest/doctest.h>

#include "audio/engine.h"
#include "platform/paths.h"
#include "scene/audio_sync.h"
#include "scene/components.h"
#include "scene/resource_table.h"
#include "scene/scene.h"
#include "scene/serialization.h"

namespace {

// Le moteur audio en mode silencieux : tout fonctionne, rien ne sort, la CI peut tourner.
struct Fixture {
    audio::Engine engine;
    scene::Scene scene;
    scene::ResourceTable resources;
    scene::ResourceHandle fire = scene::kInvalidResource;

    bool start() {
        if (!engine.create(true)) {
            return false;
        }
        const audio::SoundHandle sound =
            engine.loadSound(platform::assetPath("audio/braises.wav"));
        if (sound == audio::kInvalidSound) {
            return false;
        }
        fire = resources.addSound("braises", sound);
        return fire != scene::kInvalidResource;
    }
};

} // namespace

TEST_CASE("A scene source starts a voice at the entity position") {
    Fixture f;
    REQUIRE(f.start());

    const scene::Entity ember = f.scene.createEntity("braise");
    // createEntity pose deja un Transform : tout objet a une place dans le monde. On
    // modifie celui-la, on n'en ajoute pas un second.
    f.scene.registry().get<scene::Transform>(ember).position = core::Vec3{2.0f, 1.0f, -4.0f};
    f.scene.registry().emplace<scene::AudioSource>(ember,
                                                   scene::AudioSource{f.fire, 0.5f, true});

    scene::startAudioSources(f.scene, f.resources, f.engine);

    const auto* voice = f.scene.registry().try_get<scene::AudioVoice>(ember);
    REQUIRE(voice != nullptr);
    CHECK(f.engine.isVoicePlaying(voice->handle));
    // Le son sort de l'entite, pas de l'origine.
    CHECK(f.engine.voicePosition(voice->handle).x == doctest::Approx(2.0f));
    CHECK(f.engine.voicePosition(voice->handle).z == doctest::Approx(-4.0f));
}

TEST_CASE("A source without a sound stays silent instead of failing") {
    Fixture f;
    REQUIRE(f.start());

    const scene::Entity ghost = f.scene.createEntity("fantome");
    // Nom de son inconnu au chargement : la poignee est invalide.
    f.scene.registry().emplace<scene::AudioSource>(
        ghost, scene::AudioSource{scene::kInvalidResource, 1.0f, true});

    scene::startAudioSources(f.scene, f.resources, f.engine);

    // Aucune voix, aucun plantage. Un niveau incomplet doit rester jouable.
    CHECK(f.scene.registry().try_get<scene::AudioVoice>(ghost) == nullptr);
    CHECK(f.engine.activeVoiceCount() == 0);
}

TEST_CASE("Sources are started once, not at every call") {
    Fixture f;
    REQUIRE(f.start());

    const scene::Entity ember = f.scene.createEntity("braise");
    f.scene.registry().emplace<scene::AudioSource>(ember,
                                                   scene::AudioSource{f.fire, 1.0f, true});

    scene::startAudioSources(f.scene, f.resources, f.engine);
    scene::startAudioSources(f.scene, f.resources, f.engine);

    // Sans l'exclusion des entites deja pourvues, chaque appel empilerait une voix de plus
    // et le budget serait consomme en quelques secondes.
    CHECK(f.engine.activeVoiceCount() == 1);
}

TEST_CASE("A voice follows the entity that carries it, through its parent") {
    Fixture f;
    REQUIRE(f.start());

    // Exactement la situation de la scene de demonstration : la braise est fille de la
    // statue, qui tourne. C'est la position MONDE qui doit s'entendre.
    const scene::Entity statue = f.scene.createEntity("statue");

    const scene::Entity ember = f.scene.createEntity("braise");
    f.scene.registry().get<scene::Transform>(ember).position = core::Vec3{2.0f, 0.0f, 0.0f};
    f.scene.registry().emplace<scene::AudioSource>(ember,
                                                   scene::AudioSource{f.fire, 1.0f, true});
    REQUIRE(f.scene.setParent(ember, statue));

    scene::startAudioSources(f.scene, f.resources, f.engine);
    const auto* voice = f.scene.registry().try_get<scene::AudioVoice>(ember);
    REQUIRE(voice != nullptr);
    CHECK(f.engine.voicePosition(voice->handle).x == doctest::Approx(2.0f));

    // Un demi-tour de la statue : la braise passe de +X a -X sans qu'on touche a son
    // propre Transform.
    f.scene.registry().get<scene::Transform>(statue).rotation =
        glm::angleAxis(core::kPi, core::Vec3{0.0f, 1.0f, 0.0f});
    f.scene.updateWorldTransforms();
    scene::syncAudioSources(f.scene, f.engine);

    CHECK(f.engine.voicePosition(voice->handle).x == doctest::Approx(-2.0f).epsilon(0.01));
}

TEST_CASE("The demo scene declares sounds the game actually registers") {
    // Garde-fou sur les DONNEES, pas sur le code : si quelqu'un renomme un son dans
    // demo.json sans toucher au jeu, la source deviendrait muette en silence. Ici, la CI
    // le voit. C'est le pendant du repli sur "missing" pour les maillages, qui rend une
    // erreur visible a l'ecran.
    scene::ResourceTable resources;
    resources.addSound("braises", 0);
    resources.addSound("souffle", 1);

    scene::Scene scene;
    REQUIRE(scene::loadSceneFromFile(scene, resources,
                                     platform::assetPath("scenes/demo.json").c_str()));

    core::u32 sources = 0;
    for (auto [entity, source] : scene.registry().view<const scene::AudioSource>().each()) {
        (void)entity;
        CHECK(source.sound != scene::kInvalidResource);
        ++sources;
    }
    CHECK(sources == 2);
}
