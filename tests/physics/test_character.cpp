#include <doctest/doctest.h>

#include "physics/world.h"

namespace {

constexpr core::f32 kStep = 1.0f / 60.0f;
constexpr core::f32 kRadius = 0.35f;
constexpr core::f32 kHeight = 1.8f;

// Sol dont la face superieure est exactement a y = 0.
void addFloor(physics::World& world) {
    world.addBox(core::Vec3{0.0f, -0.5f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{20.0f, 0.5f, 20.0f}, true);
}

void simulate(physics::World& world, core::u32 steps) {
    for (core::u32 i = 0; i < steps; ++i) {
        world.step(kStep);
    }
}

} // namespace

TEST_CASE("A character falls and lands on the floor") {
    physics::World world;
    REQUIRE(world.create());
    addFloor(world);

    const physics::CharacterHandle player =
        world.addCharacter(core::Vec3{0.0f, 3.0f, 0.0f}, kRadius, kHeight);
    REQUIRE(player != physics::kInvalidCharacter);
    CHECK_FALSE(world.characterOnGround(player));

    // La gravite n'est pas appliquee par le monde : c'est l'appelant qui decide de la
    // vitesse, exactement comme le fait le jeu. Un personnage doit rester pilotable.
    for (core::u32 i = 0; i < 120; ++i) {
        core::Vec3 velocity = world.characterVelocity(player);
        if (!world.characterOnGround(player)) {
            velocity += world.gravity() * kStep;
        } else {
            velocity.y = 0.0f;
        }
        world.setCharacterVelocity(player, velocity);
        world.step(kStep);
    }

    // Les pieds reposent sur le sol : la position d'un personnage designe sa base, pas le
    // centre de sa capsule.
    CHECK(world.characterPosition(player).y == doctest::Approx(0.0f).epsilon(0.05));
    CHECK(world.characterOnGround(player));
}

TEST_CASE("A wall stops a character") {
    physics::World world;
    REQUIRE(world.create());
    addFloor(world);
    // Mur dont la face interieure est a x = 3.
    world.addBox(core::Vec3{3.5f, 1.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{0.5f, 2.0f, 10.0f}, true);

    const physics::CharacterHandle player =
        world.addCharacter(core::Vec3{0.0f, 0.0f, 0.0f}, kRadius, kHeight);

    // Cinq secondes de marche vers le mur, soit 20 m demandes pour 3 m disponibles.
    for (core::u32 i = 0; i < 300; ++i) {
        world.setCharacterVelocity(player, core::Vec3{4.0f, 0.0f, 0.0f});
        world.step(kStep);
    }

    const core::f32 x = world.characterPosition(player).x;
    // Arrete par le mur, a un rayon de capsule pres. Sans collision, il serait a x = 20.
    CHECK(x < 3.0f - kRadius + 0.05f);
    CHECK(x > 2.0f);
}

TEST_CASE("A character slides along a wall instead of sticking to it") {
    physics::World world;
    REQUIRE(world.create());
    addFloor(world);
    world.addBox(core::Vec3{3.5f, 1.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{0.5f, 2.0f, 10.0f}, true);

    const physics::CharacterHandle player =
        world.addCharacter(core::Vec3{0.0f, 0.0f, 0.0f}, kRadius, kHeight);

    // On pousse en diagonale contre le mur : la composante perpendiculaire est bloquee,
    // la composante parallele doit passer. C'est ce qui evite de rester colle a un mur
    // qu'on longe, defaut tres penible dans un couloir.
    //
    // Deux secondes seulement : le mur ne fait que 20 m de long, et un trajet plus long
    // ferait contourner son extremite - le test ne mesurerait alors plus rien.
    for (core::u32 i = 0; i < 120; ++i) {
        world.setCharacterVelocity(player, core::Vec3{4.0f, 0.0f, 3.0f});
        world.step(kStep);
    }

    CHECK(world.characterPosition(player).x < 3.0f);
    CHECK(world.characterPosition(player).z > 4.0f);
}
