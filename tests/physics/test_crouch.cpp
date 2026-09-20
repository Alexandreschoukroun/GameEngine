#include <doctest/doctest.h>

#include "physics/world.h"

namespace {

constexpr core::f32 kStep = 1.0f / 60.0f;
constexpr core::f32 kRadius = 0.35f;
constexpr core::f32 kStanding = 1.8f;
constexpr core::f32 kCrouched = 1.0f;

// Un sol, et de quoi poser un plafond bas au-dessus du personnage.
void addFloor(physics::World& world) {
    world.addBox(core::Vec3{0.0f, -0.5f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{20.0f, 0.5f, 20.0f}, true);
}

physics::CharacterHandle addPlayer(physics::World& world, const core::Vec3& feet) {
    return world.addCharacter(feet, kRadius, kStanding);
}

} // namespace

TEST_CASE("Crouching shortens the character without moving its feet") {
    physics::World world;
    REQUIRE(world.create());
    addFloor(world);
    const physics::CharacterHandle player = addPlayer(world, core::Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(player != physics::kInvalidCharacter);
    CHECK(world.characterHeight(player) == doctest::Approx(kStanding));

    const core::Vec3 before = world.characterPosition(player);
    REQUIRE(world.setCharacterHeight(player, kCrouched));
    CHECK(world.characterHeight(player) == doctest::Approx(kCrouched));

    // Les pieds ne bougent pas : la capsule raccourcit PAR LE HAUT. Si elle raccourcissait
    // par le bas, s'accroupir ferait s'enfoncer le joueur dans le sol, et se relever le
    // projetterait en l'air.
    const core::Vec3 after = world.characterPosition(player);
    CHECK(after.y == doctest::Approx(before.y).epsilon(0.02));
    CHECK(after.x == doctest::Approx(before.x));
}

TEST_CASE("Standing up again is allowed when there is room") {
    physics::World world;
    REQUIRE(world.create());
    addFloor(world);
    const physics::CharacterHandle player = addPlayer(world, core::Vec3{0.0f, 0.0f, 0.0f});

    REQUIRE(world.setCharacterHeight(player, kCrouched));
    CHECK(world.setCharacterHeight(player, kStanding));
    CHECK(world.characterHeight(player) == doctest::Approx(kStanding));
}

TEST_CASE("A low ceiling refuses the stand-up") {
    physics::World world;
    REQUIRE(world.create());
    addFloor(world);
    const physics::CharacterHandle player = addPlayer(world, core::Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(world.setCharacterHeight(player, kCrouched));

    // Une dalle a 1,2 m du sol : de quoi passer accroupi, pas de quoi se redresser.
    world.addBox(core::Vec3{0.0f, 1.3f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{3.0f, 0.1f, 3.0f}, true);
    for (core::u32 i = 0; i < 10; ++i) {
        world.step(kStep);
    }

    // C'est Jolt qui tranche, en testant la forme contre le decor reel. Un rayon vers le
    // haut pourrait passer entre deux obstacles et conclure a tort qu'on peut se lever.
    CHECK_FALSE(world.setCharacterHeight(player, kStanding));
    // Et le refus laisse le personnage dans son etat precedent, pas a moitie redresse.
    CHECK(world.characterHeight(player) == doctest::Approx(kCrouched));
}

TEST_CASE("Crouched, the character passes under an obstacle that blocks him standing") {
    physics::World world;
    REQUIRE(world.create());
    addFloor(world);
    const physics::CharacterHandle player = addPlayer(world, core::Vec3{0.0f, 0.0f, -3.0f});

    // Un linteau : un bloc suspendu entre 1,2 m et 2,4 m, laissant un passage sous lui.
    world.addBox(core::Vec3{0.0f, 1.8f, 0.0f}, core::Quat(1, 0, 0, 0),
                 core::Vec3{3.0f, 0.6f, 0.3f}, true);

    // Debout, on s'y cogne.
    for (core::u32 i = 0; i < 180; ++i) {
        world.setCharacterVelocity(player, core::Vec3{0.0f, 0.0f, 2.0f});
        world.step(kStep);
    }
    const core::f32 blocked = world.characterPosition(player).z;
    CHECK(blocked < 0.0f);

    // Accroupi, on passe.
    REQUIRE(world.setCharacterHeight(player, kCrouched));
    for (core::u32 i = 0; i < 180; ++i) {
        world.setCharacterVelocity(player, core::Vec3{0.0f, 0.0f, 2.0f});
        world.step(kStep);
    }
    CHECK(world.characterPosition(player).z > 0.5f);
}

TEST_CASE("An impossible height is refused") {
    physics::World world;
    REQUIRE(world.create());
    addFloor(world);
    const physics::CharacterHandle player = addPlayer(world, core::Vec3{0.0f, 0.0f, 0.0f});

    // Plus courte que deux rayons, la capsule n'a plus de corps : ses deux calottes se
    // recouvriraient.
    CHECK_FALSE(world.setCharacterHeight(player, 2.0f * kRadius));
    CHECK_FALSE(world.setCharacterHeight(player, 0.0f));
    CHECK(world.characterHeight(player) == doctest::Approx(kStanding));

    // Un identifiant invalide ne doit rien casser.
    CHECK_FALSE(world.setCharacterHeight(physics::kInvalidCharacter, kCrouched));
}
