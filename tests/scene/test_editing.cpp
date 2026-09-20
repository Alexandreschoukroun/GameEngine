#include <doctest/doctest.h>

#include "scene/audio_sync.h"
#include "scene/components.h"
#include "scene/editing.h"
#include "scene/footsteps.h"
#include "scene/physics_sync.h"
#include "scene/sector_graph.h"

#include <vector>

TEST_CASE("Descendants are found through the whole chain") {
    scene::Scene scene;
    const scene::Entity grandparent = scene.createEntity("grand-parent");
    const scene::Entity parent = scene.createEntity("parent");
    const scene::Entity child = scene.createEntity("enfant");
    const scene::Entity stranger = scene.createEntity("etranger");
    REQUIRE(scene.setParent(parent, grandparent));
    REQUIRE(scene.setParent(child, parent));

    std::vector<scene::Entity> found;
    scene::collectDescendants(scene, grandparent, found);

    // Les petits-enfants comptent : une hierarchie n'a aucune raison de s'arreter a un
    // niveau, et s'y arreter laisserait des orphelins a la destruction.
    CHECK(found.size() == 2);
    CHECK(std::find(found.begin(), found.end(), parent) != found.end());
    CHECK(std::find(found.begin(), found.end(), child) != found.end());
    // Une entite n'est pas sa propre descendante, et l'etranger n'a rien a y faire.
    CHECK(std::find(found.begin(), found.end(), grandparent) == found.end());
    CHECK(std::find(found.begin(), found.end(), stranger) == found.end());

    // Une feuille n'a pas de descendants, et une entite morte non plus.
    scene::collectDescendants(scene, child, found);
    CHECK(found.empty());
    scene::collectDescendants(scene, scene::kInvalidEntity, found);
    CHECK(found.empty());
}

TEST_CASE("Duplicating copies the data and gives a fresh identifier") {
    scene::Scene scene;
    const scene::Entity source = scene.createEntity("caisse");
    scene.registry().get<scene::Transform>(source).position = core::Vec3{1.0f, 2.0f, 3.0f};
    scene.registry().emplace<scene::Collider>(
        source, scene::Collider{scene::ColliderShape::Box, core::Vec3{0.3f, 0.3f, 0.3f},
                                false, 400.0f});

    const scene::Entity copy = scene::duplicateEntity(scene, source);
    REQUIRE(copy != scene::kInvalidEntity);
    CHECK(copy != source);

    CHECK(scene.registry().get<scene::Transform>(copy).position.y == doctest::Approx(2.0f));
    const auto* collider = scene.registry().try_get<scene::Collider>(copy);
    REQUIRE(collider != nullptr);
    CHECK(collider->density == doctest::Approx(400.0f));
    CHECK_FALSE(collider->isStatic);

    // L'identifiant est NEUF : c'est la cle du fichier de scene, et deux entites qui la
    // partageraient rendraient la sauvegarde ambigue.
    CHECK(scene.registry().get<scene::Id>(copy).value !=
          scene.registry().get<scene::Id>(source).value);
}

TEST_CASE("Duplicating brings the children along, relinked to the copy") {
    scene::Scene scene;
    const scene::Entity door = scene.createEntity("porte");
    const scene::Entity handle = scene.createEntity("poignee");
    REQUIRE(scene.setParent(handle, door));

    const scene::Entity copiedDoor = scene::duplicateEntity(scene, door);
    REQUIRE(copiedDoor != scene::kInvalidEntity);

    std::vector<scene::Entity> children;
    scene::collectDescendants(scene, copiedDoor, children);
    // Copier une porte sans sa poignee donnerait un objet incomplet.
    REQUIRE(children.size() == 1);

    // Et surtout : la poignee copiee suit la porte COPIEE. Si elle pointait encore sur
    // l'originale, ouvrir une porte ferait bouger la poignee de l'autre.
    CHECK(scene.registry().get<scene::Parent>(children[0]).value == copiedDoor);
    CHECK(scene.registry().get<scene::Parent>(handle).value == door);

    // L'original garde exactement un enfant : la duplication ne lui en vole aucun.
    std::vector<scene::Entity> originalChildren;
    scene::collectDescendants(scene, door, originalChildren);
    CHECK(originalChildren.size() == 1);
    CHECK(originalChildren[0] == handle);
}

TEST_CASE("Duplicating a child alone keeps it attached to the same parent") {
    scene::Scene scene;
    const scene::Entity door = scene.createEntity("porte");
    const scene::Entity handle = scene.createEntity("poignee");
    REQUIRE(scene.setParent(handle, door));

    const scene::Entity copy = scene::duplicateEntity(scene, handle);
    REQUIRE(copy != scene::kInvalidEntity);
    // Le parent est EXTERIEUR a la copie : le lien se copie tel quel. Dupliquer une
    // poignee doit donner une seconde poignee sur la meme porte.
    CHECK(scene.registry().get<scene::Parent>(copy).value == door);
}

TEST_CASE("Every data component survives a duplication") {
    // Ce test verrouille la liste explicite de copyDataComponents : ajouter un composant
    // au moteur sans l'y declarer le ferait disparaitre en silence a la duplication.
    scene::Scene scene;
    const scene::Entity source = scene.createEntity("tout");
    scene.registry().emplace<scene::MeshRenderer>(source, scene::MeshRenderer{1, 2});
    scene.registry().emplace<scene::LightSource>(source, scene::LightSource{});
    scene.registry().emplace<scene::Collider>(source, scene::Collider{});
    scene.registry().emplace<scene::Hinge>(source, scene::Hinge{});
    scene.registry().emplace<scene::Surface>(source, scene::Surface{7});
    scene.registry().emplace<scene::AudioSource>(source, scene::AudioSource{3, 0.5f, true});
    scene.registry().emplace<scene::Sector>(source, scene::Sector{});

    const scene::Entity copy = scene::duplicateEntity(scene, source);
    REQUIRE(copy != scene::kInvalidEntity);

    const entt::registry& registry = scene.registry();
    CHECK(registry.try_get<scene::MeshRenderer>(copy) != nullptr);
    CHECK(registry.try_get<scene::LightSource>(copy) != nullptr);
    CHECK(registry.try_get<scene::Collider>(copy) != nullptr);
    CHECK(registry.try_get<scene::Hinge>(copy) != nullptr);
    CHECK(registry.try_get<scene::Surface>(copy) != nullptr);
    CHECK(registry.try_get<scene::AudioSource>(copy) != nullptr);
    CHECK(registry.try_get<scene::Sector>(copy) != nullptr);
    CHECK(registry.get<scene::Surface>(copy).footstep == 7);
    CHECK(registry.get<scene::MeshRenderer>(copy).material == 2);
}

TEST_CASE("Runtime components are not copied") {
    scene::Scene scene;
    const scene::Entity source = scene.createEntity("caisse");
    // Un corps physique et une voix audio n'existent qu'a l'execution : les copier
    // designerait la ressource de l'ORIGINAL, et arreter le son de la copie couperait
    // celui de l'autre.
    scene.registry().emplace<scene::PhysicsBody>(source, scene::PhysicsBody{42});
    scene.registry().emplace<scene::AudioVoice>(source, scene::AudioVoice{7});

    const scene::Entity copy = scene::duplicateEntity(scene, source);
    REQUIRE(copy != scene::kInvalidEntity);
    CHECK(scene.registry().try_get<scene::PhysicsBody>(copy) == nullptr);
    CHECK(scene.registry().try_get<scene::AudioVoice>(copy) == nullptr);
}

TEST_CASE("Destroying an entity takes its descendants with it") {
    scene::Scene scene;
    const scene::Entity door = scene.createEntity("porte");
    const scene::Entity handle = scene.createEntity("poignee");
    const scene::Entity screw = scene.createEntity("vis");
    const scene::Entity other = scene.createEntity("caisse");
    REQUIRE(scene.setParent(handle, door));
    REQUIRE(scene.setParent(screw, handle));

    scene::destroyEntityTree(scene, door);

    // Detruire seulement la porte laisserait la poignee pointer sur un parent mort : elle
    // sauterait a l'origine du monde, ce qui a l'air d'un bug et n'en est pas un.
    CHECK_FALSE(scene.isValid(door));
    CHECK_FALSE(scene.isValid(handle));
    CHECK_FALSE(scene.isValid(screw));
    CHECK(scene.isValid(other));
}

TEST_CASE("Destroying a child leaves its parent alone") {
    scene::Scene scene;
    const scene::Entity door = scene.createEntity("porte");
    const scene::Entity handle = scene.createEntity("poignee");
    REQUIRE(scene.setParent(handle, door));

    scene::destroyEntityTree(scene, handle);
    CHECK(scene.isValid(door));
    CHECK_FALSE(scene.isValid(handle));

    // Detruire une entite morte ne doit rien casser : l'editeur peut le demander deux
    // fois sur un clic reste enfonce.
    scene::destroyEntityTree(scene, handle);
    CHECK(scene.isValid(door));
}
