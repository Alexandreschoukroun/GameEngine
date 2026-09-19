#include <doctest/doctest.h>

#include "physics/world.h"

#include <cmath>

namespace {

constexpr core::f32 kStep = 1.0f / 60.0f;

// Masse volumique d'une porte en bois plein. Avec celle de Jolt par defaut (1000, celle
// de l'eau), ce battant pesait 144 kg et son inertie avoisinait 39 kg.m2 : il ne bougeait
// pas d'un centimetre sous une impulsion humaine. A l'inverse, a 150 il s'envolait.
constexpr core::f32 kDoorDensity = 300.0f; // bois plein : ~43 kg pour ce battant

// Battant de 0,9 m de large, 2 m de haut, 8 cm d'epaisseur, charniere sur son bord gauche.
struct Door {
    physics::BodyHandle body = physics::kInvalidBody;
    core::Vec3 center{0.0f, 0.0f, 0.0f};
    core::Vec3 hinge{-0.45f, 0.0f, 0.0f};
};

Door makeDoor(physics::World& world, core::f32 minAngle, core::f32 maxAngle,
              core::f32 friction) {
    Door door;
    door.body = world.addBox(door.center, core::Quat(1, 0, 0, 0),
                             core::Vec3{0.45f, 1.0f, 0.04f}, false, kDoorDensity);
    world.addHinge(door.body, door.hinge, core::Vec3{0.0f, 1.0f, 0.0f}, minAngle, maxAngle,
                   friction);
    return door;
}

// Angle de la porte autour de son axe vertical, deduit de sa rotation.
core::f32 doorAngle(const physics::World& world, const Door& door) {
    const core::Vec3 normal = world.bodyRotation(door.body) * core::Vec3{0.0f, 0.0f, 1.0f};
    return std::atan2(normal.x, normal.z);
}

} // namespace

TEST_CASE("Pulling on the free edge makes the door swing") {
    physics::World world;
    REQUIRE(world.create());
    const Door door = makeDoor(world, -1.6f, 1.6f, 0.0f);

    CHECK(doorAngle(world, door) == doctest::Approx(0.0f).epsilon(0.02));

    // Impulsion au bord LIBRE, a l'oppose des gonds : c'est le bras de levier qui cree le
    // couple. La meme impulsion appliquee sur l'axe ne ferait presque rien.
    //
    // L'ordre de grandeur se calcule : 43 kg, une inertie d'environ 12 kg.m2 autour des
    // gonds, donc 12 N.s a 45 cm donnent un demi-tour par seconde. Un geste humain.
    world.applyImpulseAtPoint(door.body, core::Vec3{0.0f, 0.0f, 12.0f},
                              core::Vec3{0.45f, 0.0f, 0.0f});
    for (core::u32 i = 0; i < 30; ++i) {
        world.step(kStep);
    }

    CHECK(std::abs(doorAngle(world, door)) > 0.15f);
}

TEST_CASE("The hinge keeps the door attached") {
    physics::World world;
    REQUIRE(world.create());
    const Door door = makeDoor(world, -1.6f, 1.6f, 0.0f);

    // Impulsion violente, vers le haut et de cote : sans charniere, la porte partirait.
    world.applyImpulseAtPoint(door.body, core::Vec3{400.0f, 600.0f, 200.0f},
                              core::Vec3{0.45f, 1.0f, 0.0f});
    for (core::u32 i = 0; i < 120; ++i) {
        world.step(kStep);
    }

    // Le bord des gonds n'a pas bouge : la porte a pivote, elle ne s'est pas envolee.
    // La borne vient de la geometrie : en pivotant d'un quart de tour, le centre du
    // battant parcourt au plus 0,45 x racine(2), soit 64 cm. Un corps libre recevant la
    // meme impulsion serait a plusieurs dizaines de metres.
    const core::Vec3 position = world.bodyPosition(door.body);
    CHECK(glm::length(position - door.center) < 0.8f);
}

TEST_CASE("Limits stop the door") {
    physics::World world;
    REQUIRE(world.create());
    // Butees serrees : la porte ne peut s'entrouvrir que d'un quart de tour dans un sens.
    const Door door = makeDoor(world, -0.8f, 0.0f, 0.0f);

    world.applyImpulseAtPoint(door.body, core::Vec3{0.0f, 0.0f, 40.0f},
                              core::Vec3{0.45f, 0.0f, 0.0f});
    for (core::u32 i = 0; i < 180; ++i) {
        world.step(kStep);
    }

    // Une porte sans butees traverserait le mur ou le chambranle. Une petite tolerance :
    // un solveur iteratif depasse legerement avant de corriger.
    const core::f32 angle = doorAngle(world, door);
    CHECK(angle < 0.05f);
    CHECK(angle > -0.9f);
}

TEST_CASE("Friction brings the door to a stop") {
    physics::World world;
    REQUIRE(world.create());
    // Butees larges, pour qu'elles ne puissent PAS etre creditees de l'arret : ce qu'on
    // mesure ici, c'est le frottement des gonds, et lui seul.
    const Door door = makeDoor(world, -3.0f, 3.0f, 6.0f);

    world.applyImpulseAtPoint(door.body, core::Vec3{0.0f, 0.0f, 12.0f},
                              core::Vec3{0.45f, 0.0f, 0.0f});

    // Trois secondes : mesure a l'appui, le battant lance a environ 1,4 rad/s s'immobilise
    // en deux secondes sous 15 N.m. Le calcul theorique en annoncait une - le solveur de
    // Jolt freine plus doucement qu'un couple constant applique a la main, et c'est la
    // mesure qui fait foi.
    for (core::u32 i = 0; i < 180; ++i) {
        world.step(kStep);
    }
    const core::f32 settled = doorAngle(world, door);

    // Elle a bel et bien tourne : sans ca, le test passerait aussi sur une porte bloquee.
    CHECK(std::abs(settled) > 0.5f);
    // Et sans toucher ses butees : c'est le frottement qui l'arrete, pas le chambranle.
    CHECK(std::abs(settled) < 2.5f);

    for (core::u32 i = 0; i < 120; ++i) {
        world.step(kStep);
    }

    // Deux secondes de plus, au meme angle. Sans frottement, le battant tournerait
    // indefiniment comme une porte de saloon sans gonds.
    CHECK(doorAngle(world, door) == doctest::Approx(settled).epsilon(0.01));
}

TEST_CASE("A hinged body is reported as such") {
    physics::World world;
    REQUIRE(world.create());
    const Door door = makeDoor(world, -1.0f, 1.0f, 1.0f);
    const physics::BodyHandle crate =
        world.addBox(core::Vec3{5.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.3f, 0.3f, 0.3f}, false);

    // C'est ce qui permet au jeu de choisir comment manipuler l'objet vise : on ne tire
    // pas sur une porte comme sur une caisse libre.
    CHECK(world.isBodyHinged(door.body));
    CHECK_FALSE(world.isBodyHinged(crate));
}
