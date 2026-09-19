#include <doctest/doctest.h>

#include "scene/scene.h"
#include "scene/sector_graph.h"

namespace {

// Cree un secteur centre sur une position, avec des demi-dimensions donnees.
scene::Entity makeSector(scene::Scene& scene, const char* name, const core::Vec3& center,
                         const core::Vec3& halfExtents) {
    const scene::Entity entity = scene.createEntity(name);
    scene.registry().get<scene::Transform>(entity).position = center;
    scene.registry().emplace<scene::Sector>(entity, scene::Sector{halfExtents});
    return entity;
}

scene::Entity makePortal(scene::Scene& scene, const char* name, scene::Entity a,
                         scene::Entity b) {
    const scene::Entity entity = scene.createEntity(name);
    scene.registry().emplace<scene::Portal>(entity,
                                            scene::Portal{a, b, core::Vec3{0.2f, 1.0f, 1.0f}});
    return entity;
}

} // namespace

TEST_CASE("A point is located in the sector that contains it") {
    scene::Scene scene;
    const scene::Entity west =
        makeSector(scene, "ouest", core::Vec3{-3.0f, 0.0f, 0.0f}, core::Vec3{3.0f, 2.0f, 6.0f});
    const scene::Entity east =
        makeSector(scene, "est", core::Vec3{3.0f, 0.0f, 0.0f}, core::Vec3{3.0f, 2.0f, 6.0f});
    scene.updateWorldTransforms();

    CHECK(scene::sectorAt(scene, core::Vec3{-4.0f, 0.0f, 1.0f}) == west);
    CHECK(scene::sectorAt(scene, core::Vec3{4.0f, 0.0f, 1.0f}) == east);
    // Au-dela du plafond du secteur : dehors, meme si X et Z sont dans les clous.
    CHECK(scene::sectorAt(scene, core::Vec3{4.0f, 9.0f, 1.0f}) == scene::kInvalidEntity);
    CHECK(scene::sectorAt(scene, core::Vec3{40.0f, 0.0f, 0.0f}) == scene::kInvalidEntity);
}

TEST_CASE("A sector follows the entity it is attached to") {
    scene::Scene scene;
    const scene::Entity ascenseur = scene.createEntity("ascenseur");
    const scene::Entity cabine =
        makeSector(scene, "cabine", core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 1.0f, 1.0f});
    REQUIRE(scene.setParent(cabine, ascenseur));

    scene.registry().get<scene::Transform>(ascenseur).position = core::Vec3{0.0f, 10.0f, 0.0f};
    scene.updateWorldTransforms();

    // Le secteur est monte avec son parent : c'est la matrice monde qui compte, pas le
    // Transform local.
    CHECK(scene::sectorAt(scene, core::Vec3{0.0f, 10.0f, 0.0f}) == cabine);
    CHECK(scene::sectorAt(scene, core::Vec3{0.0f, 0.0f, 0.0f}) == scene::kInvalidEntity);
}

TEST_CASE("Reachability stops at the requested number of portals") {
    scene::Scene scene;
    // Trois pieces en enfilade : A - B - C.
    const scene::Entity a =
        makeSector(scene, "a", core::Vec3{-10.0f, 0.0f, 0.0f}, core::Vec3{2.0f, 2.0f, 2.0f});
    const scene::Entity b =
        makeSector(scene, "b", core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{2.0f, 2.0f, 2.0f});
    const scene::Entity c =
        makeSector(scene, "c", core::Vec3{10.0f, 0.0f, 0.0f}, core::Vec3{2.0f, 2.0f, 2.0f});
    makePortal(scene, "ab", a, b);
    makePortal(scene, "bc", b, c);

    std::vector<scene::Entity> reachable;

    // Aucun portail franchi : on ne voit que sa propre piece.
    scene::reachableSectors(scene, a, 0, reachable);
    CHECK(reachable.size() == 1);
    CHECK(reachable[0] == a);

    // Un portail : la piece voisine, pas celle d'apres. C'est exactement ce qui permettra
    // au rendu de n'afficher que le necessaire, et au son de s'attenuer par etapes.
    scene::reachableSectors(scene, a, 1, reachable);
    CHECK(reachable.size() == 2);

    scene::reachableSectors(scene, a, 2, reachable);
    CHECK(reachable.size() == 3);

    // Depuis la piece du milieu, les deux voisines en un seul portail.
    scene::reachableSectors(scene, b, 1, reachable);
    CHECK(reachable.size() == 3);
}

TEST_CASE("A cycle of sectors does not loop forever") {
    scene::Scene scene;
    const scene::Entity a =
        makeSector(scene, "a", core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 1.0f, 1.0f});
    const scene::Entity b =
        makeSector(scene, "b", core::Vec3{5.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 1.0f, 1.0f});
    const scene::Entity c =
        makeSector(scene, "c", core::Vec3{10.0f, 0.0f, 0.0f}, core::Vec3{1.0f, 1.0f, 1.0f});
    // Boucle fermee : a - b - c - a. Un parcours naif y tournerait indefiniment.
    makePortal(scene, "ab", a, b);
    makePortal(scene, "bc", b, c);
    makePortal(scene, "ca", c, a);

    std::vector<scene::Entity> reachable;
    scene::reachableSectors(scene, a, 10, reachable);

    CHECK(reachable.size() == 3);
}

TEST_CASE("Reachability from an invalid sector is empty") {
    const scene::Scene scene;
    std::vector<scene::Entity> reachable;

    scene::reachableSectors(scene, scene::kInvalidEntity, 3, reachable);

    CHECK(reachable.empty());
}
