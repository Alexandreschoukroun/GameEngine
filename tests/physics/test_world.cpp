#include <doctest/doctest.h>

#include "physics/world.h"

namespace {

constexpr core::f32 kFixedStep = 1.0f / 60.0f;

void simulate(physics::World& world, core::u32 steps) {
    for (core::u32 i = 0; i < steps; ++i) {
        world.step(kFixedStep);
    }
}

} // namespace

TEST_CASE("A dynamic body falls under gravity") {
    physics::World world;
    REQUIRE(world.create());

    const physics::BodyHandle box =
        world.addBox(core::Vec3{0.0f, 10.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.5f, 0.5f, 0.5f}, false);
    REQUIRE(box != physics::kInvalidBody);

    simulate(world, 30); // une demi-seconde

    // Chute libre : environ 1,2 m en 0,5 s. On verifie l'ordre de grandeur, pas la valeur
    // exacte - un test qui fige le resultat d'un integrateur casse a chaque mise a jour.
    const core::f32 y = world.bodyPosition(box).y;
    CHECK(y < 9.5f);
    CHECK(y > 8.0f);
}

TEST_CASE("A static body never moves") {
    physics::World world;
    REQUIRE(world.create());

    const physics::BodyHandle wall =
        world.addBox(core::Vec3{0.0f, 3.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{1.0f, 1.0f, 1.0f}, true);
    simulate(world, 60);

    CHECK(world.bodyPosition(wall).y == doctest::Approx(3.0f));
}

TEST_CASE("A falling body comes to rest on a static floor") {
    physics::World world;
    REQUIRE(world.create());

    world.addBox(core::Vec3{0.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{10.0f, 0.5f, 10.0f}, true);
    const physics::BodyHandle box =
        world.addBox(core::Vec3{0.0f, 4.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.5f, 0.5f, 0.5f}, false);

    simulate(world, 180); // trois secondes : largement de quoi tomber et s'immobiliser

    // Le sol s'arrete a y = 0,5 et la caisse fait 0,5 de demi-hauteur : elle repose donc
    // autour de y = 1. La tolerance couvre l'enfoncement que Jolt autorise au contact.
    const core::f32 y = world.bodyPosition(box).y;
    CHECK(y > 0.9f);
    CHECK(y < 1.1f);
}

TEST_CASE("The same simulation twice gives the same result") {
    const auto runOnce = [] {
        physics::World world;
        world.create();
        world.addBox(core::Vec3{0.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{10.0f, 0.5f, 10.0f}, true);
        const physics::BodyHandle box =
            world.addBox(core::Vec3{0.3f, 5.0f, -0.2f},
                         glm::angleAxis(0.4f, glm::normalize(core::Vec3{1.0f, 1.0f, 0.0f})),
                         core::Vec3{0.5f, 0.5f, 0.5f}, false);
        simulate(world, 120);
        return world.bodyPosition(box);
    };

    // Le determinisme a pas fixe est la raison d'etre de la boucle construite en M0, et la
    // condition pour qu'une partie rejouee se comporte pareil.
    const core::Vec3 first = runOnce();
    const core::Vec3 second = runOnce();

    CHECK(first.x == doctest::Approx(second.x));
    CHECK(first.y == doctest::Approx(second.y));
    CHECK(first.z == doctest::Approx(second.z));
}
