#include <doctest/doctest.h>

#include "audio/engine.h"
#include "physics/world.h"
#include "platform/paths.h"
#include "scene/footsteps.h"
#include "scene/physics_sync.h"
#include "scene/resource_table.h"
#include "scene/scene.h"

namespace {

// Un sol, un joueur qui marche dessus, et de quoi savoir ce qu'on a entendu.
struct Ground {
    audio::Engine audio;
    physics::World physics;
    scene::Scene scene;
    scene::ResourceTable resources;
    scene::FootstepPlayer footsteps;
    scene::ResourceHandle stone = scene::kInvalidResource;
    scene::ResourceHandle wood = scene::kInvalidResource;

    bool start() {
        if (!audio.create(true) || !physics.create()) {
            return false;
        }
        const audio::SoundHandle stoneSound =
            audio.loadSound(platform::assetPath("audio/pas_pierre.wav"));
        const audio::SoundHandle woodSound =
            audio.loadSound(platform::assetPath("audio/pas_bois.wav"));
        if (stoneSound == audio::kInvalidSound || woodSound == audio::kInvalidSound) {
            return false;
        }
        stone = resources.addSound("pas_pierre", stoneSound);
        wood = resources.addSound("pas_bois", woodSound);
        footsteps.configure(1.0f, 0.5f); // une foulee d'un metre : les calculs sont lisibles
        return true;
    }

    // Une dalle a y = 0 (sa face superieure), eventuellement dotee d'une matiere.
    scene::Entity addSlab(const char* name, core::f32 centerX, scene::ResourceHandle sound) {
        const scene::Entity entity = scene.createEntity(name);
        scene.registry().get<scene::Transform>(entity).position =
            core::Vec3{centerX, -0.5f, 0.0f};
        scene.registry().emplace<scene::Collider>(
            entity, scene::Collider{scene::ColliderShape::Box,
                                    core::Vec3{2.0f, 0.5f, 4.0f}, true, 1000.0f});
        if (sound != scene::kInvalidResource) {
            scene.registry().emplace<scene::Surface>(entity, scene::Surface{sound});
        }
        scene::createPhysicsBodies(scene, physics);
        return entity;
    }

    // Marche d'une distance donnee, en une seule enjambee.
    scene::ResourceHandle walk(core::f32 distance, const core::Vec3& feet,
                               bool onGround = true) {
        return footsteps.update(scene, physics, resources, audio, feet, onGround, distance);
    }
};

} // namespace

TEST_CASE("A step is played once the stride length is covered") {
    Ground g;
    REQUIRE(g.start());
    g.addSlab("dalle", 0.0f, g.stone);
    const core::Vec3 feet{0.0f, 0.0f, 0.0f};

    // Trois demi-foulees : le pas tombe sur la deuxieme, pas avant.
    CHECK(g.walk(0.5f, feet) == scene::kInvalidResource);
    CHECK(g.walk(0.5f, feet) == g.stone);
    CHECK(g.walk(0.5f, feet) == scene::kInvalidResource);
}

TEST_CASE("Standing still never plays a step") {
    Ground g;
    REQUIRE(g.start());
    g.addSlab("dalle", 0.0f, g.stone);

    for (core::u32 i = 0; i < 120; ++i) {
        CHECK(g.walk(0.0f, core::Vec3{0.0f, 0.0f, 0.0f}) == scene::kInvalidResource);
    }
    CHECK(g.audio.activeVoiceCount() == 0);
}

TEST_CASE("The material under the feet decides which sound is played") {
    Ground g;
    REQUIRE(g.start());
    // Deux dalles cote a cote, comme le sol de la scene de demonstration : pierre d'un
    // cote, bois de l'autre.
    g.addSlab("dalle_pierre", -2.0f, g.stone);
    g.addSlab("dalle_bois", 2.0f, g.wood);

    CHECK(g.walk(1.0f, core::Vec3{-2.0f, 0.0f, 0.0f}) == g.stone);
    CHECK(g.walk(1.0f, core::Vec3{2.0f, 0.0f, 0.0f}) == g.wood);
}

TEST_CASE("A floor without a declared material stays silent") {
    Ground g;
    REQUIRE(g.start());
    // Dalle sans composant Surface : on prefere le silence a un son arbitraire, qui
    // annoncerait au joueur une matiere que le niveau n'a pas decrite.
    g.addSlab("dalle_nue", 0.0f, scene::kInvalidResource);

    CHECK(g.walk(2.0f, core::Vec3{0.0f, 0.0f, 0.0f}) == scene::kInvalidResource);
    CHECK(g.audio.activeVoiceCount() == 0);
}

TEST_CASE("Walking in the air plays nothing, and landing starts a fresh stride") {
    Ground g;
    REQUIRE(g.start());
    g.addSlab("dalle", 0.0f, g.stone);
    const core::Vec3 feet{0.0f, 0.0f, 0.0f};

    CHECK(g.walk(0.9f, feet, false) == scene::kInvalidResource);
    // Le compteur est reparti de zero : sans ca, l'atterrissage declencherait un pas
    // immediat, comme si le joueur avait marche pendant son saut.
    CHECK(g.walk(0.5f, feet) == scene::kInvalidResource);
    CHECK(g.walk(0.5f, feet) == g.stone);
}

TEST_CASE("Repeated steps are not identical") {
    Ground g;
    REQUIRE(g.start());
    g.addSlab("dalle", 0.0f, g.stone);
    const core::Vec3 feet{0.0f, 0.0f, 0.0f};

    // Chaque pas occupe sa propre voix : elles coexistent, ce qui prouve que le moteur
    // ne recycle pas betement la meme. La variation de hauteur, elle, n'est pas
    // observable de l'exterieur - c'est le prix a payer pour ne pas exposer l'interne.
    for (core::u32 i = 0; i < 4; ++i) {
        CHECK(g.walk(1.0f, feet) == g.stone);
    }
    CHECK(g.audio.activeVoiceCount() == 4);
}

TEST_CASE("A body maps back to the entity that owns it") {
    Ground g;
    REQUIRE(g.start());
    const scene::Entity slab = g.addSlab("dalle", 0.0f, g.stone);

    const auto* body = g.scene.registry().try_get<scene::PhysicsBody>(slab);
    REQUIRE(body != nullptr);
    CHECK(scene::entityForBody(g.scene, body->handle) == slab);
    CHECK(scene::entityForBody(g.scene, physics::kInvalidBody) == scene::kInvalidEntity);
}
