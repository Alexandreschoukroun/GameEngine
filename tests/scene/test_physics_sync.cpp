#include <doctest/doctest.h>

#include "scene/physics_sync.h"
#include "scene/resource_table.h"
#include "scene/serialization.h"

namespace {

scene::Entity makeBody(scene::Scene& scene, const char* name, const core::Vec3& position,
                       bool isStatic) {
    const scene::Entity entity = scene.createEntity(name);
    scene.registry().get<scene::Transform>(entity).position = position;
    scene.registry().emplace<scene::Collider>(
        entity, scene::Collider{scene::ColliderShape::Box, core::Vec3{0.5f, 0.5f, 0.5f},
                                isStatic});
    return entity;
}

} // namespace

TEST_CASE("Colliders survive a save and load cycle") {
    scene::Scene original;
    const scene::ResourceTable resources;
    const scene::Entity box = makeBody(original, "caisse", core::Vec3{1.0f, 2.0f, 3.0f}, false);
    original.registry().get<scene::Collider>(box).halfExtents = core::Vec3{0.3f, 0.7f, 0.2f};

    const std::string saved = scene::saveSceneToString(original, resources);

    scene::Scene reloaded;
    REQUIRE(scene::loadSceneFromString(reloaded, resources, saved));

    const scene::Entity loaded = reloaded.findByName("caisse");
    REQUIRE(loaded != scene::kInvalidEntity);
    const auto* collider = reloaded.registry().try_get<scene::Collider>(loaded);
    REQUIRE(collider != nullptr);
    CHECK(collider->halfExtents.y == doctest::Approx(0.7f));
    CHECK_FALSE(collider->isStatic);

    // Le corps physique n'est PAS serialise : un identifiant de corps n'existe qu'a
    // l'execution, il n'a aucun sens dans un fichier.
    CHECK(reloaded.registry().try_get<scene::PhysicsBody>(loaded) == nullptr);
}

TEST_CASE("Bodies are created once per collider") {
    scene::Scene scene;
    physics::World world;
    REQUIRE(world.create());

    makeBody(scene, "sol", core::Vec3{0.0f, 0.0f, 0.0f}, true);
    makeBody(scene, "caisse", core::Vec3{0.0f, 5.0f, 0.0f}, false);

    scene::createPhysicsBodies(scene, world);
    const auto countBodies = [&scene] {
        core::u32 count = 0;
        for (auto entity : scene.registry().view<const scene::PhysicsBody>()) {
            (void)entity;
            ++count;
        }
        return count;
    };
    CHECK(countBodies() == 2);

    // Deuxieme appel : rien de neuf a creer. Sans ce filtrage, chaque frame ajouterait un
    // corps de plus au meme endroit.
    scene::createPhysicsBodies(scene, world);
    CHECK(countBodies() == 2);
}

TEST_CASE("Only dynamic bodies move the transform") {
    scene::Scene scene;
    physics::World world;
    REQUIRE(world.create());

    const scene::Entity ground = makeBody(scene, "sol", core::Vec3{0.0f, 0.0f, 0.0f}, true);
    const scene::Entity crate = makeBody(scene, "caisse", core::Vec3{0.0f, 5.0f, 0.0f}, false);
    scene::createPhysicsBodies(scene, world);

    for (core::u32 i = 0; i < 120; ++i) {
        world.step(1.0f / 60.0f);
    }
    scene::syncTransformsFromPhysics(scene, world);

    // La caisse est tombee sur le sol statique et s'y est arretee.
    const core::f32 crateY = scene.registry().get<scene::Transform>(crate).position.y;
    CHECK(crateY < 5.0f);
    CHECK(crateY > 0.5f);

    // Le sol n'a pas bouge d'un millimetre : recopier sa pose serait du travail perdu.
    CHECK(scene.registry().get<scene::Transform>(ground).position.y == doctest::Approx(0.0f));
}
