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

TEST_CASE("A body can be teleported, and it does not keep its momentum") {
    physics::World world;
    REQUIRE(world.create());
    world.addBox(core::Vec3{0.0f, -1.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{20.0f, 0.5f, 20.0f}, true);

    const physics::BodyHandle crate =
        world.addBox(core::Vec3{0.0f, 5.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.3f, 0.3f, 0.3f}, false);

    // On le laisse tomber : il acquiert une vitesse vers le bas.
    for (core::u32 i = 0; i < 30; ++i) {
        world.step(1.0f / 60.0f);
    }
    REQUIRE(world.bodyVelocity(crate).y < -1.0f);

    // Puis on le replace a la main, comme le fait l'editeur.
    world.setBodyTransform(crate, core::Vec3{4.0f, 3.0f, -2.0f}, core::Quat(1, 0, 0, 0));
    CHECK(world.bodyPosition(crate).x == doctest::Approx(4.0f));
    CHECK(world.bodyPosition(crate).z == doctest::Approx(-2.0f));
    // La vitesse est annulee : sans cela, l'objet replace repartirait avec l'elan qu'il
    // avait des qu'on le lache, et paraitrait glisser tout seul.
    CHECK(glm::length(world.bodyVelocity(crate)) == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("Teleporting a static body does not fail") {
    physics::World world;
    REQUIRE(world.create());
    const physics::BodyHandle wall =
        world.addBox(core::Vec3{0.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{1.0f, 1.0f, 1.0f}, true);

    // Un corps statique n'a pas de vitesse a annuler, et Jolt refuse qu'on lui en donne :
    // l'implementation doit le distinguer, sinon deplacer un mur dans l'editeur
    // declencherait une assertion.
    world.setBodyTransform(wall, core::Vec3{3.0f, 1.0f, 0.0f}, core::Quat(1, 0, 0, 0));
    CHECK(world.bodyPosition(wall).x == doctest::Approx(3.0f));
    CHECK_FALSE(world.isBodyDynamic(wall));
}
