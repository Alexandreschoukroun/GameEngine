#include <doctest/doctest.h>

#include "physics/world.h"

TEST_CASE("A ray finds the body in front of it") {
    physics::World world;
    REQUIRE(world.create());

    const physics::BodyHandle box =
        world.addBox(core::Vec3{0.0f, 0.0f, -5.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{1.0f, 1.0f, 1.0f}, true);

    const physics::RayHit hit =
        world.raycast(core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{0.0f, 0.0f, -1.0f}, 10.0f);

    REQUIRE(hit.hit);
    CHECK(hit.body == box);
    // La boite fait 1 m de demi-cote : sa face avant est a 4 m.
    CHECK(hit.distance == doctest::Approx(4.0f).epsilon(0.02));
    // La normale pointe vers le rayon, donc vers +Z.
    CHECK(hit.normal.z == doctest::Approx(1.0f).epsilon(0.05));
}

TEST_CASE("A ray that reaches nothing reports no hit") {
    physics::World world;
    REQUIRE(world.create());
    world.addBox(core::Vec3{0.0f, 0.0f, -50.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{1.0f, 1.0f, 1.0f}, true);

    // Portee trop courte : l'objet existe, mais hors d'atteinte. C'est ce qui empechera
    // d'attraper un objet a l'autre bout d'une piece.
    const physics::RayHit hit =
        world.raycast(core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{0.0f, 0.0f, -1.0f}, 5.0f);

    CHECK_FALSE(hit.hit);
    CHECK(hit.body == physics::kInvalidBody);
}

TEST_CASE("A ray stops at the nearest body") {
    physics::World world;
    REQUIRE(world.create());

    const physics::BodyHandle near =
        world.addBox(core::Vec3{0.0f, 0.0f, -3.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.5f, 0.5f, 0.5f}, true);
    world.addBox(core::Vec3{0.0f, 0.0f, -6.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{0.5f, 0.5f, 0.5f}, true);

    const physics::RayHit hit =
        world.raycast(core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{0.0f, 0.0f, -1.0f}, 20.0f);

    // C'est ce qui rendra l'antagoniste de M7 aveugle derriere un mur : le premier corps
    // touche bloque la vue.
    REQUIRE(hit.hit);
    CHECK(hit.body == near);
}

TEST_CASE("Only dynamic bodies can be grabbed") {
    physics::World world;
    REQUIRE(world.create());

    const physics::BodyHandle wall =
        world.addBox(core::Vec3{0.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{1.0f, 1.0f, 1.0f}, true);
    const physics::BodyHandle crate =
        world.addBox(core::Vec3{5.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.3f, 0.3f, 0.3f}, false);

    CHECK_FALSE(world.isBodyDynamic(wall));
    CHECK(world.isBodyDynamic(crate));
}

TEST_CASE("Setting a velocity wakes a sleeping body") {
    physics::World world;
    REQUIRE(world.create());
    world.addBox(core::Vec3{0.0f, -0.5f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{10.0f, 0.5f, 10.0f}, true);
    const physics::BodyHandle crate =
        world.addBox(core::Vec3{0.0f, 1.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.3f, 0.3f, 0.3f}, false);

    // Trois secondes : la caisse tombe, se pose, et Jolt finit par l'endormir pour ne plus
    // la simuler.
    for (core::u32 i = 0; i < 180; ++i) {
        world.step(1.0f / 60.0f);
    }
    const core::f32 restingX = world.bodyPosition(crate).x;

    world.setBodyVelocity(crate, core::Vec3{3.0f, 0.0f, 0.0f});
    for (core::u32 i = 0; i < 30; ++i) {
        world.step(1.0f / 60.0f);
    }

    // Sans le reveil explicite, un corps endormi ignorerait la consigne : la caisse
    // resterait figee malgre la poussee.
    CHECK(world.bodyPosition(crate).x > restingX + 0.5f);
}
