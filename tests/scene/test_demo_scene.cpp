#include <doctest/doctest.h>

#include "platform/paths.h"
#include "scene/audio_sync.h"
#include "scene/components.h"
#include "scene/footsteps.h"
#include "scene/physics_sync.h"
#include "scene/resource_table.h"
#include "scene/scene.h"
#include "scene/serialization.h"

// Garde-fou sur les DONNEES, pas sur le code.
//
// La scene de demonstration cite des ressources par leur nom logique. Si quelqu'un en
// renomme une dans demo.json sans toucher au jeu, rien ne le signale : pas d'erreur de
// compilation, pas de plantage - juste une source muette ou un objet qui disparait. Ces
// tests transforment ce silence en echec.

namespace {

// Une geometrie de collision minimale mais VALIDE. Son contenu n'a aucune importance :
// ce qu'on verifie ici, ce sont les noms, pas les triangles.
const core::Vec3 kDummyPositions[3] = {
    {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
const core::u32 kDummyIndices[3] = {0, 1, 2};

// Les ressources que le jeu enregistre a l'initialisation. Les poignees valent ce qu'on
// veut : ce qu'on verifie ici, c'est que les NOMS se correspondent.
scene::ResourceTable gameResources() {
    scene::ResourceTable resources;
    resources.addSound("braises", 0);
    resources.addSound("souffle", 1);
    resources.addSound("pas_pierre", 2);
    resources.addSound("pas_bois", 3);
    // Les matieres libres, chargees depuis assets/textures/<nom>/.
    resources.addMaterial("beton", scene::Material{});
    resources.addMaterial("plancher", scene::Material{});
    resources.addMaterial("metal_rouille", scene::Material{});
    resources.addMaterial("suzanne", scene::Material{});
    // Matieres declarees par les fichiers glTF eux-memes.
    resources.addMaterial("loquet", scene::Material{});

    scene::CollisionMesh geometry;
    geometry.positions = kDummyPositions;
    geometry.vertexCount = 3;
    geometry.indices = kDummyIndices;
    geometry.indexCount = 3;
    resources.addCollisionMesh("piece", geometry);
    return resources;
}

bool loadDemo(scene::Scene& scene, const scene::ResourceTable& resources) {
    return scene::loadSceneFromFile(scene, resources,
                                    platform::assetPath("scenes/demo.json").c_str());
}

} // namespace

TEST_CASE("Every sound the demo scene names is one the game registers") {
    const scene::ResourceTable resources = gameResources();
    scene::Scene scene;
    REQUIRE(loadDemo(scene, resources));

    core::u32 sources = 0;
    for (auto [entity, source] : scene.registry().view<const scene::AudioSource>().each()) {
        (void)entity;
        CHECK(source.sound != scene::kInvalidResource);
        ++sources;
    }
    CHECK(sources == 2);

    // Meme garde-fou pour les matieres du sol : une surface dont le son est introuvable
    // rendrait le joueur silencieux sans que rien ne le signale.
    core::u32 surfaces = 0;
    for (auto [entity, surface] : scene.registry().view<const scene::Surface>().each()) {
        (void)entity;
        CHECK(surface.footstep != scene::kInvalidResource);
        ++surfaces;
    }
    CHECK(surfaces == 2);
}

TEST_CASE("Every displayed entity cites a material the game registers") {
    const scene::ResourceTable resources = gameResources();
    scene::Scene scene;
    REQUIRE(loadDemo(scene, resources));

    // Une entite dont le materiau est introuvable serait purement et simplement sautee
    // par le rendu, en silence.
    core::u32 rendered = 0;
    for (auto [entity, renderer] : scene.registry().view<const scene::MeshRenderer>().each()) {
        (void)entity;
        CHECK(renderer.material != scene::kInvalidResource);
        ++rendered;
    }
    CHECK(rendered == 11);
}

TEST_CASE("Every mesh collider names a geometry the game registers") {
    const scene::ResourceTable resources = gameResources();
    scene::Scene scene;
    REQUIRE(loadDemo(scene, resources));

    core::u32 meshColliders = 0;
    for (auto [entity, collider] : scene.registry().view<const scene::Collider>().each()) {
        (void)entity;
        if (collider.shape != scene::ColliderShape::Mesh) {
            continue;
        }
        // Une geometrie introuvable laisse l'entite SANS COLLISION : on traverserait les
        // murs, et rien ne le signalerait avant d'y marcher.
        CHECK(collider.collisionMesh != scene::kInvalidResource);
        // Un maillage de triangles est forcement statique, quoi que dise le fichier.
        CHECK(collider.isStatic);
        ++meshColliders;
    }
    // La piece entiere, en un seul collider, la ou il fallait sept boites.
    CHECK(meshColliders == 1);
}

TEST_CASE("The door handle sits on the free edge and follows the door") {
    const scene::ResourceTable resources = gameResources();
    scene::Scene scene;
    REQUIRE(loadDemo(scene, resources));

    const scene::Entity door = scene.findByName("porte");
    const scene::Entity handle = scene.findByName("poignee");
    REQUIRE(door != scene::kInvalidEntity);
    REQUIRE(handle != scene::kInvalidEntity);

    // La poignee est un ENFANT : elle n'a aucun code propre, elle suit le battant parce
    // que la hierarchie de M3 le fait pour elle.
    const auto* parent = scene.registry().try_get<scene::Parent>(handle);
    REQUIRE(parent != nullptr);
    CHECK(parent->value == door);

    scene.updateWorldTransforms();
    const core::Vec3 doorCenter(scene.worldMatrix(door)[3]);
    const core::Vec3 handleCenter(scene.worldMatrix(handle)[3]);

    // Les gonds sont a 45 cm a gauche du centre : la poignee doit etre de l'autre cote,
    // sans quoi elle n'offrirait aucun bras de levier - c'est toute la physique de 5.3.
    //
    // La borne porte sur la MOITIE du battant, et non sur une distance precise : ce qu'on
    // mesure ici est l'ORIGINE du modele, qui n'est pas son milieu, et une rotation
    // deplace l'une par rapport a l'autre. Exiger une distance au centimetre reviendrait
    // a figer une orientation, alors que la propriete qui compte est "du cote du bord
    // libre, et sur le battant".
    const core::f32 offset = handleCenter.x - doorCenter.x;
    CHECK(offset > 0.0f);
    CHECK(offset < 0.45f);

    // Un enfant herite de l'echelle de son parent. Le battant est mis a l'echelle
    // (0,9 ; 2,0 ; 0,08) - un modele importe y serait donc ecrase en plaque. L'echelle
    // locale du loquet ANNULE celle du parent, si bien qu'il conserve sa taille reelle,
    // celle que son fichier decrit.
    const core::Mat4 world = scene.worldMatrix(handle);
    const core::f32 width = glm::length(core::Vec3(world[0]));
    const core::f32 height = glm::length(core::Vec3(world[1]));
    const core::f32 depth = glm::length(core::Vec3(world[2]));
    CHECK(width == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(height == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(depth == doctest::Approx(1.0f).epsilon(0.01));
}
