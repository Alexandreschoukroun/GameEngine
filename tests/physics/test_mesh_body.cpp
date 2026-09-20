#include <doctest/doctest.h>

#include "physics/world.h"

#include <vector>

namespace {

constexpr core::f32 kStep = 1.0f / 60.0f;

// Une dalle de deux triangles, a plat en y = 0, de 10 m de cote.
//
// C'est la geometrie minimale qui ait un sens ici : un sol. Le decor d'un vrai niveau
// n'en est que la version a cent mille triangles.
struct Quad {
    std::vector<core::Vec3> vertices{
        {-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {-5.0f, 0.0f, 5.0f}};
    // L'ORDRE des indices decide de quel cote la face regarde : le produit vectoriel des
    // deux premieres aretes donne la normale. Une collision de maillage est a FACE UNIQUE
    // - enroulee a l'envers, cette dalle laisserait tout la traverser, et un rayon venu
    // d'en haut rapporterait une normale pointant vers le bas.
    std::vector<core::u32> indices{0, 2, 1, 0, 3, 2};
};

physics::BodyHandle addFloor(physics::World& world, const Quad& quad,
                             const core::Vec3& scale = core::Vec3{1.0f, 1.0f, 1.0f}) {
    return world.addMesh(core::Vec3{0.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0), scale,
                         quad.vertices.data(), static_cast<core::u32>(quad.vertices.size()),
                         quad.indices.data(), static_cast<core::u32>(quad.indices.size()));
}

} // namespace

TEST_CASE("A falling body lands on a mesh floor") {
    physics::World world;
    REQUIRE(world.create());
    const Quad quad;
    REQUIRE(addFloor(world, quad) != physics::kInvalidBody);

    const physics::BodyHandle crate =
        world.addBox(core::Vec3{0.0f, 3.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                     core::Vec3{0.3f, 0.3f, 0.3f}, false);

    for (core::u32 i = 0; i < 180; ++i) {
        world.step(kStep);
    }

    // Elle repose SUR la dalle : son centre est a une demi-hauteur au-dessus, et elle ne
    // l'a pas traversee. C'est toute la difference entre une collision de maillage qui
    // marche et une qui laisse tout tomber a l'infini.
    const core::Vec3 position = world.bodyPosition(crate);
    CHECK(position.y == doctest::Approx(0.3f).epsilon(0.15));
    CHECK(position.y > 0.0f);
}

TEST_CASE("A ray hits the mesh where the geometry actually is") {
    physics::World world;
    REQUIRE(world.create());
    const Quad quad;
    REQUIRE(addFloor(world, quad) != physics::kInvalidBody);

    const physics::RayHit hit =
        world.raycast(core::Vec3{1.5f, 4.0f, -2.0f}, core::Vec3{0.0f, -1.0f, 0.0f}, 10.0f);
    REQUIRE(hit.hit);
    CHECK(hit.distance == doctest::Approx(4.0f).epsilon(0.01));
    // La normale pointe vers le haut : le rayon a bien touche la face superieure.
    CHECK(hit.normal.y > 0.9f);

    // Au-dela du bord de la dalle, il n'y a plus rien - un maillage n'est pas un plan
    // infini, contrairement a ce qu'on suppose souvent d'un sol.
    const physics::RayHit miss =
        world.raycast(core::Vec3{20.0f, 4.0f, 0.0f}, core::Vec3{0.0f, -1.0f, 0.0f}, 10.0f);
    CHECK_FALSE(miss.hit);
}

TEST_CASE("Scaling a mesh collider moves its surface") {
    physics::World world;
    REQUIRE(world.create());
    const Quad quad;
    // Trois fois plus haut : la dalle est a plat en y = 0, donc l'echelle verticale ne la
    // deplace pas, mais l'echelle horizontale doit etendre sa portee.
    REQUIRE(addFloor(world, quad, core::Vec3{3.0f, 1.0f, 3.0f}) != physics::kInvalidBody);

    // A 12 m du centre, la dalle non mise a l'echelle ne couvrait rien (elle s'arrete a 5).
    // Mise a l'echelle par trois, elle s'etend jusqu'a 15.
    const physics::RayHit hit =
        world.raycast(core::Vec3{12.0f, 4.0f, 0.0f}, core::Vec3{0.0f, -1.0f, 0.0f}, 10.0f);
    CHECK(hit.hit);
}

TEST_CASE("Invalid geometry is refused instead of crashing") {
    physics::World world;
    REQUIRE(world.create());
    const Quad quad;

    // Un nombre d'indices qui n'est pas un multiple de trois decrit un triangle tronque.
    CHECK(world.addMesh(core::Vec3{0.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                        core::Vec3{1.0f, 1.0f, 1.0f}, quad.vertices.data(), 4,
                        quad.indices.data(), 5) == physics::kInvalidBody);

    // Un indice qui designe un sommet inexistant : Jolt lirait hors des bornes.
    const std::vector<core::u32> broken{0, 1, 99};
    CHECK(world.addMesh(core::Vec3{0.0f, 0.0f, 0.0f}, core::Quat(1, 0, 0, 0),
                        core::Vec3{1.0f, 1.0f, 1.0f}, quad.vertices.data(), 4,
                        broken.data(), 3) == physics::kInvalidBody);

    // Une echelle negative retourne les triangles : le decor deviendrait traversable, et
    // le defaut ne se verrait qu'en jouant.
    CHECK(addFloor(world, quad, core::Vec3{1.0f, -1.0f, 1.0f}) == physics::kInvalidBody);

    // Rien de tout cela n'a empeche le monde de continuer a fonctionner.
    CHECK(addFloor(world, quad) != physics::kInvalidBody);
}

TEST_CASE("A mesh body is static, so it never falls") {
    physics::World world;
    REQUIRE(world.create());
    const Quad quad;
    const physics::BodyHandle floor = addFloor(world, quad);
    REQUIRE(floor != physics::kInvalidBody);

    // Un maillage de triangles n'a ni volume ni masse bien definis : aucun moteur ne sait
    // le faire rouler. Le decor ne bouge pas, c'est donc exactement ce qu'il faut.
    CHECK_FALSE(world.isBodyDynamic(floor));

    for (core::u32 i = 0; i < 120; ++i) {
        world.step(kStep);
    }
    CHECK(world.bodyPosition(floor).y == doctest::Approx(0.0f));
}
