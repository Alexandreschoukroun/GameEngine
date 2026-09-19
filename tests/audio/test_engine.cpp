#include <doctest/doctest.h>

#include "audio/engine.h"
#include "platform/paths.h"

namespace {

// Tous ces tests ouvrent le moteur en mode silencieux : miniaudio simule une carte son.
// C'est ce qui les rend executables en CI, sur une machine qui n'en a aucune.
bool startSilent(audio::Engine& engine) { return engine.create(true); }

std::string firePath() { return platform::assetPath("audio/braises.wav"); }

} // namespace

TEST_CASE("The audio engine opens and closes") {
    audio::Engine engine;
    REQUIRE(startSilent(engine));
    CHECK(engine.activeVoiceCount() == 0);
    engine.destroy();
    // Fermer deux fois ne doit rien casser : le jeu peut quitter par plusieurs chemins.
    engine.destroy();
}

TEST_CASE("A missing file yields no sound") {
    audio::Engine engine;
    REQUIRE(startSilent(engine));

    // Un fichier absent ne doit pas faire tomber le jeu. Comme pour les ressources de
    // scene en M3, l'absence se signale et se contourne.
    CHECK(engine.loadSound("ce/fichier/n/existe/pas.wav") == audio::kInvalidSound);
    CHECK(engine.play(audio::kInvalidSound, core::Vec3{0.0f, 0.0f, 0.0f}) ==
          audio::kInvalidVoice);
}

TEST_CASE("The same file is decoded once") {
    audio::Engine engine;
    REQUIRE(startSilent(engine));

    const audio::SoundHandle first = engine.loadSound(firePath());
    REQUIRE(first != audio::kInvalidSound);
    // Deux demandes du meme fichier donnent le meme identifiant : les echantillons ne sont
    // en memoire qu'une fois, meme si cent entites jouent ce son.
    CHECK(engine.loadSound(firePath()) == first);
}

TEST_CASE("Playing a sound occupies a voice, stopping frees it") {
    audio::Engine engine;
    REQUIRE(startSilent(engine));
    const audio::SoundHandle fire = engine.loadSound(firePath());
    REQUIRE(fire != audio::kInvalidSound);

    const audio::VoiceHandle voice = engine.play(fire, core::Vec3{2.0f, 0.0f, -3.0f}, true);
    REQUIRE(voice != audio::kInvalidVoice);
    CHECK(engine.isVoicePlaying(voice));
    CHECK(engine.activeVoiceCount() == 1);

    engine.stop(voice);
    CHECK_FALSE(engine.isVoicePlaying(voice));
    CHECK(engine.activeVoiceCount() == 0);
}

TEST_CASE("A stale voice handle cannot touch its successor") {
    audio::Engine engine;
    REQUIRE(startSilent(engine));
    const audio::SoundHandle fire = engine.loadSound(firePath());
    REQUIRE(fire != audio::kInvalidSound);

    const audio::VoiceHandle first = engine.play(fire, core::Vec3{0.0f, 0.0f, 0.0f}, true);
    engine.stop(first);
    const audio::VoiceHandle second = engine.play(fire, core::Vec3{0.0f, 0.0f, 0.0f}, true);

    // La deuxieme voix reprend le meme emplacement, mais pas le meme identifiant : la
    // generation a avance. Sans elle, arreter `first` couperait `second`, et le bug se
    // manifesterait par un son qui se tait sans raison, des mois plus tard.
    REQUIRE(second != audio::kInvalidVoice);
    CHECK(first != second);
    CHECK_FALSE(engine.isVoicePlaying(first));
    CHECK(engine.isVoicePlaying(second));

    engine.stop(first); // sans effet
    CHECK(engine.isVoicePlaying(second));
}

TEST_CASE("The voice budget is enforced") {
    audio::Engine engine;
    REQUIRE(startSilent(engine));
    const audio::SoundHandle fire = engine.loadSound(firePath());
    REQUIRE(fire != audio::kInvalidSound);

    for (core::u32 i = 0; i < audio::kMaxVoices; ++i) {
        REQUIRE(engine.play(fire, core::Vec3{0.0f, 0.0f, 0.0f}, true) != audio::kInvalidVoice);
    }
    CHECK(engine.activeVoiceCount() == audio::kMaxVoices);

    // Au-dela du budget, on refuse proprement. Voler la voix d'un son en cours
    // s'entendrait davantage que l'absence du nouveau.
    CHECK(engine.play(fire, core::Vec3{0.0f, 0.0f, 0.0f}, true) == audio::kInvalidVoice);
}
