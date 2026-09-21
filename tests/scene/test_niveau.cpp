#include <doctest/doctest.h>

#include "assets/mesh_data.h"
#include "physics/world.h"
#include "rhi/mesh.h"
#include "platform/paths.h"
#include "scene/components.h"
#include "scene/footsteps.h"
#include "scene/physics_sync.h"
#include "scene/resource_table.h"
#include "scene/scene.h"
#include "scene/serialization.h"

#include <array>
#include <string>
#include <vector>

// Le niveau est le premier decor du moteur qui ne soit pas ecrit a la main : il sort de
// tools/generate_level.py. Un generateur se trompe silencieusement - une erreur ne
// produit pas de message, elle produit un mur.
//
// Ces tests ne regardent donc pas le code du generateur, mais son RESULTAT, en le faisant
// jouer : on y pose un personnage, on le fait marcher, et on sonde les murs au rayon.
//
// Les deux controles ne disent pas la meme chose, et c'est une lecon payee comptant. En
// retournant volontairement l'enroulement des 188 triangles de mur, le personnage a
// continue d'etre arrete : un CharacterVirtual heurte AUSSI les faces arriere, par
// defaut. Marcher dans le niveau ne prouve donc PAS que ses faces sont a l'endroit.
//
// Ce qui le prouve, c'est la NORMALE rapportee par un rayon. Le rayon touche le mur dans
// les deux cas ; mais sur une face retournee il revient avec une normale qui s'eloigne de
// nous au lieu de nous faire face. C'est exactement la quantite dont le rendu se sert
// pour eliminer les faces arriere et pour eclairer : une normale a l'envers, c'est un mur
// absent a l'ecran. Le rayon est donc le seul temoin, dans un test sans GPU, de ce qu'on
// verra reellement.

namespace {

constexpr core::f32 kStep = 1.0f / 60.0f;
constexpr core::f32 kRadius = 0.35f; // les memes que le joueur du jeu : un test sur un
constexpr core::f32 kHeight = 1.8f;  // gabarit different ne prouverait rien sur celui-la

// Le point d'apparition declare par le jeu, dans le hall. Le sol du niveau est a y = 0.
constexpr core::Vec3 kSpawn{0.0f, 0.05f, 2.0f};

// Les quatre maillages du niveau, un par matiere. Le decoupage vient d'une contrainte du
// rendu - une entite ne porte qu'un materiau - mais il se retrouve ici parce que la
// collision suit exactement la meme geometrie.
constexpr const char* kLevelMeshes[] = {"niveau_beton", "niveau_platre", "niveau_carrelage",
                                        "niveau_plancher"};

// Porte la geometrie du niveau ET la table qui la cite. La table ne garde que des
// pointeurs : si les MeshData mouraient avant elle, on lirait de la memoire liberee.
struct Level {
    std::vector<assets::MeshData> geometry;
    // Des maillages d'AFFICHAGE vides. Ces tests tournent sans GPU - c'est ce qui leur
    // permet de passer en integration continue - donc aucun de ces objets n'est cree :
    // un rhi::Mesh par defaut n'est qu'une poignee nulle, sans destructeur qui appellerait
    // OpenGL. Ils sont la pour que les NOMS cites par la scene aient quelque chose a
    // resoudre : sans eux, le chargeur remplace silencieusement le maillage manquant par
    // un substitut, et le test ne verrait plus la difference entre un nom juste et un nom
    // faux.
    std::array<rhi::Mesh, 4> display;
    scene::ResourceTable resources;
    scene::Scene scene;
};

// Charge le niveau comme le jeu le fait : les memes fichiers, les memes noms logiques.
//
// Les sons et les matieres sont declares avec des poignees quelconques - ce qu'on verifie
// a travers eux, ce sont les NOMS. La geometrie de collision, elle, est chargee pour de
// bon : c'est elle qu'on va eprouver.
bool loadLevel(Level& level) {
    for (const char* name : kLevelMeshes) {
        const std::string path = std::string("models/niveau/") + name + ".gltf";
        assets::MeshData mesh;
        if (!assets::loadGltfMesh(platform::assetPath(path.c_str()).c_str(), mesh)) {
            return false;
        }
        level.geometry.push_back(std::move(mesh));
    }

    for (core::u32 i = 0; i < static_cast<core::u32>(level.geometry.size()); ++i) {
        const assets::MeshData& mesh = level.geometry[i];
        scene::CollisionMesh collision;
        collision.positions = mesh.positions.data();
        collision.vertexCount = static_cast<core::u32>(mesh.positions.size());
        collision.indices = mesh.indices.data();
        collision.indexCount = static_cast<core::u32>(mesh.indices.size());
        level.resources.addCollisionMesh(kLevelMeshes[i], collision);

        scene::MeshBounds bounds;
        bounds.valid = mesh.computeBounds(bounds.min, bounds.max);
        level.resources.addMesh(kLevelMeshes[i], &level.display[i], bounds);
    }

    level.resources.addMaterial("beton", scene::Material{});
    level.resources.addMaterial("platre", scene::Material{});
    level.resources.addMaterial("carrelage", scene::Material{});
    level.resources.addMaterial("plancher", scene::Material{});
    level.resources.addSound("pas_pierre", 0);
    level.resources.addSound("pas_bois", 1);

    return scene::loadSceneFromFile(level.scene, level.resources,
                                    platform::assetPath("scenes/niveau.json").c_str());
}

// Fait avancer le personnage dans une direction horizontale, gravite comprise.
//
// C'est volontairement la meme boucle que le jeu : le monde n'applique pas la gravite
// lui-meme, l'appelant decide de la vitesse. Un personnage doit rester pilotable, y
// compris en chute.
core::Vec3 walk(physics::World& world, physics::CharacterHandle player,
                const core::Vec3& direction, core::f32 speed, core::u32 steps) {
    for (core::u32 i = 0; i < steps; ++i) {
        core::Vec3 velocity = direction * speed;
        if (world.characterOnGround(player)) {
            velocity.y = 0.0f;
        } else {
            velocity.y = world.characterVelocity(player).y + world.gravity().y * kStep;
        }
        world.setCharacterVelocity(player, velocity);
        world.step(kStep);
    }
    return world.characterPosition(player);
}

// Monte le niveau dans un monde physique et y depose le joueur a son point d'apparition.
physics::CharacterHandle inhabit(Level& level, physics::World& world) {
    scene::createPhysicsBodies(level.scene, world, level.resources);
    return world.addCharacter(kSpawn, kRadius, kHeight);
}

} // namespace

TEST_CASE("Le niveau genere se charge et ne cite que des ressources connues") {
    Level level;
    REQUIRE(loadLevel(level));

    // Un collider de maillage dont la geometrie est introuvable laisse l'entite SANS
    // collision : on traverserait le decor de part en part, sans le moindre message.
    core::u32 meshColliders = 0;
    for (auto [entity, collider] :
         level.scene.registry().view<const scene::Collider>().each()) {
        (void)entity;
        if (collider.shape != scene::ColliderShape::Mesh) {
            continue;
        }
        CHECK(collider.collisionMesh != scene::kInvalidResource);
        CHECK(collider.isStatic);
        ++meshColliders;
    }
    CHECK(meshColliders == 4);

    core::u32 rendered = 0;
    for (auto [entity, renderer] :
         level.scene.registry().view<const scene::MeshRenderer>().each()) {
        (void)entity;
        CHECK(renderer.mesh != scene::kInvalidResource);
        CHECK(renderer.material != scene::kInvalidResource);
        ++rendered;
    }
    CHECK(rendered == 4);

    // Le son des pas depend de la surface foulee : sans lui, le joueur marcherait en
    // silence sur un sol pourtant sonore.
    core::u32 surfaces = 0;
    for (auto [entity, surface] : level.scene.registry().view<const scene::Surface>().each()) {
        (void)entity;
        CHECK(surface.footstep != scene::kInvalidResource);
        ++surfaces;
    }
    CHECK(surfaces == 4);

    // Un interieur sans lumiere est un ecran noir. On verifie la propriete, pas le
    // compte exact : ajouter une lampe est un geste d'auteur, pas une regression.
    core::u32 lights = 0;
    for (auto [entity, light] : level.scene.registry().view<const scene::LightSource>().each()) {
        (void)entity;
        CHECK(light.intensity > 0.0f);
        ++lights;
    }
    CHECK(lights > 0);
}

TEST_CASE("Le joueur tient debout sur le sol du niveau") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    REQUIRE(world.create());
    const physics::CharacterHandle player = inhabit(level, world);
    REQUIRE(player != physics::kInvalidCharacter);

    // Deux secondes sans rien demander : s'il n'y avait pas de sol, ou s'il etait enroule
    // a l'envers, la gravite l'aurait fait tomber d'une vingtaine de metres.
    const core::Vec3 feet = walk(world, player, core::Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 120);

    CHECK(world.characterOnGround(player));
    CHECK(feet.y > -0.05f);
    CHECK(feet.y < 0.10f);
}

TEST_CASE("Les murs du niveau presentent leur face avant a la piece") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    REQUIRE(world.create());
    scene::createPhysicsBodies(level.scene, world, level.resources);

    // Depuis le milieu du hall, a hauteur d'oeil, vers chacun de ses quatre murs. Le hall
    // occupe x de -4 a 4 et z de 0 a 6 : chaque mur est a portee, et on connait d'avance
    // la distance et la normale attendues.
    struct Probe {
        core::Vec3 direction;
        core::f32 distance;
    };
    const core::Vec3 middle{0.0f, 1.6f, 3.0f};
    const Probe probes[] = {
        {{-1.0f, 0.0f, 0.0f}, 4.0f}, // mur ouest
        {{1.0f, 0.0f, 0.0f}, 4.0f},  // mur est
        {{0.0f, 0.0f, -1.0f}, 3.0f}, // mur sud
        {{0.0f, 1.0f, 0.0f}, 1.4f},  // plafond
        {{0.0f, -1.0f, 0.0f}, 1.6f}, // sol
    };

    for (const Probe& probe : probes) {
        const physics::RayHit hit = world.raycast(middle, probe.direction, 12.0f);
        // La surface existe, et a la bonne distance : le generateur a bien ferme la piece.
        REQUIRE(hit.hit);
        CHECK(hit.distance == doctest::Approx(probe.distance).epsilon(0.02));
        // Et elle nous FAIT FACE : sa normale remonte le rayon. C'est l'assertion qui
        // porte tout le poids ici - une face retournee est touchee comme les autres, mais
        // rend une normale opposee, et disparait a l'ecran.
        CHECK(glm::dot(hit.normal, probe.direction) < -0.99f);
    }
}

TEST_CASE("Un mur du niveau arrete le joueur") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    REQUIRE(world.create());
    const physics::CharacterHandle player = inhabit(level, world);
    REQUIRE(player != physics::kInvalidCharacter);

    walk(world, player, core::Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 60); // se poser d'abord

    // Le hall s'arrete a x = -4. On marche droit dessus pendant quatre secondes, soit
    // trois fois la distance a parcourir : ce qui arrete le joueur ne peut etre que le
    // mur.
    const core::Vec3 feet = walk(world, player, core::Vec3{-1.0f, 0.0f, 0.0f}, 3.0f, 240);

    CHECK(feet.x > -4.0f);
    // Il doit s'arreter contre le mur, a un rayon pres - pas s'y enfoncer, pas rester
    // bloque a mi-chemin.
    CHECK(feet.x < -4.0f + kRadius + 0.2f);
    CHECK(world.characterOnGround(player));
}

TEST_CASE("L'embrasure du niveau laisse passer le joueur") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    REQUIRE(world.create());
    const physics::CharacterHandle player = inhabit(level, world);
    REQUIRE(player != physics::kInvalidCharacter);

    walk(world, player, core::Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 60);

    // Le hall communique avec le couloir par une embrasure en z = 6, centree sur x = 0.
    // Un mur perce est la seule chose qui distingue un niveau d'une collection de boites
    // fermees : si l'embrasure n'etait pas un vrai trou, le joueur buterait a z ~ 6.
    const core::Vec3 feet = walk(world, player, core::Vec3{0.0f, 0.0f, 1.0f}, 3.0f, 180);

    CHECK(feet.z > 8.0f);
    // Et il est passe SANS deriver : une embrasure de 2 m de large ne pardonne pas un
    // decalage d'un metre.
    CHECK(feet.x > -1.0f);
    CHECK(feet.x < 1.0f);
    CHECK(world.characterOnGround(player));
}
