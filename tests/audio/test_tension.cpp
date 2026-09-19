#include <doctest/doctest.h>

#include "audio/engine.h"
#include "audio/tension.h"
#include "platform/paths.h"

namespace {

using audio::TensionLayer;
using Layer = TensionLayer::Layer;

std::array<audio::SoundHandle, 3> loadLayers(audio::Engine& engine) {
    return {
        engine.loadSound(platform::assetPath("audio/tension_calme.wav")),
        engine.loadSound(platform::assetPath("audio/tension_pouls.wav")),
        engine.loadSound(platform::assetPath("audio/tension_aigu.wav")),
    };
}

} // namespace

TEST_CASE("The calm layer is never silent, the edge layer only appears late") {
    // La regle de melange se teste seule, sans moteur audio : c'est elle qui decide de ce
    // qu'on entend, et elle ne depend d'aucun peripherique.
    CHECK(TensionLayer::layerVolume(Layer::Calm, 0.0f) > 0.0f);
    CHECK(TensionLayer::layerVolume(Layer::Calm, 1.0f) > 0.0f);

    // Au repos, rien d'autre que le lit grave : une tension nulle doit s'entendre calme.
    CHECK(TensionLayer::layerVolume(Layer::Pulse, 0.0f) == doctest::Approx(0.0f));
    CHECK(TensionLayer::layerVolume(Layer::Edge, 0.0f) == doctest::Approx(0.0f));

    // A mi-course, le pouls est la, la dissonance pas encore. Sans ce decalage, il ne
    // resterait rien a escalader dans le dernier tiers.
    CHECK(TensionLayer::layerVolume(Layer::Pulse, 0.5f) > 0.0f);
    CHECK(TensionLayer::layerVolume(Layer::Edge, 0.5f) == doctest::Approx(0.0f));

    CHECK(TensionLayer::layerVolume(Layer::Edge, 1.0f) > 0.0f);
}

TEST_CASE("Layer volumes only ever grow with tension, except the calm bed") {
    core::f32 previousPulse = -1.0f;
    core::f32 previousEdge = -1.0f;
    core::f32 previousCalm = 1e9f;

    // Une couche qui monterait puis redescendrait ferait entendre un relachement au
    // moment ou la situation empire.
    for (core::u32 step = 0; step <= 20; ++step) {
        const core::f32 t = static_cast<core::f32>(step) / 20.0f;
        const core::f32 pulse = TensionLayer::layerVolume(Layer::Pulse, t);
        const core::f32 edge = TensionLayer::layerVolume(Layer::Edge, t);
        const core::f32 calm = TensionLayer::layerVolume(Layer::Calm, t);
        CHECK(pulse >= previousPulse);
        CHECK(edge >= previousEdge);
        // Le lit grave, lui, s'efface pour laisser la place aux autres.
        CHECK(calm <= previousCalm);
        previousPulse = pulse;
        previousEdge = edge;
        previousCalm = calm;
    }
}

TEST_CASE("Tension is clamped to its range") {
    TensionLayer tension;
    tension.setTension(5.0f);
    CHECK(tension.target() == doctest::Approx(1.0f));
    tension.setTension(-3.0f);
    CHECK(tension.target() == doctest::Approx(0.0f));
}

TEST_CASE("The three layers start together and drive the mix") {
    audio::Engine engine;
    REQUIRE(engine.create(true));
    TensionLayer tension;
    REQUIRE(tension.start(engine, loadLayers(engine)));

    // Trois voix, en permanence : les faire entrer et sortir produirait un raccord
    // audible a chaque variation, et les desynchroniserait.
    CHECK(engine.activeVoiceCount() == 3);

    tension.setTension(1.0f);
    for (core::u32 i = 0; i < 300; ++i) {
        tension.update(engine, 1.0f / 60.0f);
    }
    CHECK(tension.tension() == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(engine.activeVoiceCount() == 3);

    tension.stop(engine);
    CHECK(engine.activeVoiceCount() == 0);
}

TEST_CASE("Tension glides toward its target instead of jumping") {
    audio::Engine engine;
    REQUIRE(engine.create(true));
    TensionLayer tension;
    REQUIRE(tension.start(engine, loadLayers(engine)));

    // La premiere application prend la consigne telle quelle : sinon chaque niveau
    // commencerait par une montee de tension que personne n'a demandee.
    CHECK(tension.tension() == doctest::Approx(0.0f));

    tension.setTension(1.0f);
    tension.update(engine, 1.0f / 60.0f);
    const core::f32 afterOneFrame = tension.tension();

    // Une tension qui saute s'entend comme une erreur, pas comme une intention.
    CHECK(afterOneFrame > 0.0f);
    CHECK(afterOneFrame < 0.2f);

    tension.stop(engine);
}

TEST_CASE("A missing layer disables the music rather than playing half of it") {
    audio::Engine engine;
    REQUIRE(engine.create(true));
    TensionLayer tension;

    std::array<audio::SoundHandle, 3> broken = loadLayers(engine);
    broken[1] = audio::kInvalidSound;

    // Deux couches sur trois donneraient un melange faux, pas une musique incomplete.
    CHECK_FALSE(tension.start(engine, broken));
    CHECK(engine.activeVoiceCount() == 0);
}
