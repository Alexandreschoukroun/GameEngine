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
// jouer : on y pose un personnage, on le fait marcher, et on sonde le batiment au rayon.
//
// Les deux controles ne disent pas la meme chose, et c'est une lecon payee comptant. En
// retournant volontairement l'enroulement des triangles de mur, le personnage a continue
// d'etre arrete : un CharacterVirtual heurte AUSSI les faces arriere, par defaut. Marcher
// dans le niveau ne prouve donc PAS que ses faces sont a l'endroit.
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
constexpr core::Vec3 kSpawn{0.0f, 0.05f, 3.0f};

// Les maillages du niveau, un par matiere. Le decoupage vient d'une contrainte du rendu -
// une entite ne porte qu'un materiau - mais il se retrouve ici parce que la collision suit
// exactement la meme geometrie.
constexpr const char* kLevelMeshes[] = {
    "niveau_beton",      "niveau_bois",    "niveau_brique",
    "niveau_carrelage",  "niveau_carrelage_mural", "niveau_papier_peint",
    "niveau_plancher",   "niveau_platre",  "niveau_platre_peint",
};
constexpr std::size_t kLevelMeshCount = sizeof(kLevelMeshes) / sizeof(kLevelMeshes[0]);

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
    std::array<rhi::Mesh, kLevelMeshCount> display;
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

    // Les matieres que le jeu charge depuis assets/textures/<nom>/.
    for (const char* material : {"beton", "bois", "brique", "carrelage", "carrelage_mural",
                                 "papier_peint", "plancher", "platre", "platre_peint"}) {
        level.resources.addMaterial(material, scene::Material{});
    }
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

// Un monde physique peuple du niveau seul, sans personnage : pour les sondes.
void build(Level& level, physics::World& world) {
    REQUIRE(world.create());
    scene::createPhysicsBodies(level.scene, world, level.resources);
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
    // Une entite par matiere, ni plus ni moins. Ajouter une matiere au generateur oblige
    // donc a l'ajouter ici et dans le jeu : c'est le prix de la regle "un materiau par
    // entite", et il vaut mieux le payer a la compilation qu'a l'ecran.
    CHECK(meshColliders == kLevelMeshCount);

    core::u32 rendered = 0;
    for (auto [entity, renderer] :
         level.scene.registry().view<const scene::MeshRenderer>().each()) {
        (void)entity;
        CHECK(renderer.mesh != scene::kInvalidResource);
        CHECK(renderer.material != scene::kInvalidResource);
        ++rendered;
    }
    CHECK(rendered == kLevelMeshCount);

    // Le son des pas depend de la surface foulee : sans lui, le joueur marcherait en
    // silence sur un sol pourtant sonore.
    core::u32 surfaces = 0;
    for (auto [entity, surface] : level.scene.registry().view<const scene::Surface>().each()) {
        (void)entity;
        CHECK(surface.footstep != scene::kInvalidResource);
        ++surfaces;
    }
    CHECK(surfaces == kLevelMeshCount);

    // Un interieur sans lumiere est un ecran noir. On verifie la propriete, pas le
    // compte exact : ajouter une lampe est un geste d'auteur, pas une regression.
    core::u32 lights = 0;
    for (auto [entity, light] :
         level.scene.registry().view<const scene::LightSource>().each()) {
        (void)entity;
        CHECK(light.intensity > 0.0f);
        ++lights;
    }
    CHECK(lights > 0);
}

TEST_CASE("Les murs du hall presentent leur face avant a la piece") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    build(level, world);

    // Depuis le hall, a hauteur d'oeil. Le hall occupe x de -7 a 7 et z de 0 a 9, sous un
    // plafond a 3,60 m : chaque surface est a portee, et on connait d'avance sa distance.
    //
    // Les distances tiennent compte de l'EPAISSEUR des murs : une cloison de 14 cm
    // presente sa face a 7 cm de la frontiere, un mur de facade de 30 cm a 15 cm. C'est
    // exactement ce qui manquait a la version precedente du niveau.
    struct Probe {
        core::Vec3 direction;
        core::f32 distance;
        const char* what;
    };
    const core::Vec3 middle{0.0f, 1.6f, 1.5f};
    const Probe probes[] = {
        {{-1.0f, 0.0f, 0.0f}, 6.93f, "cloison ouest, vers le bureau"},
        {{1.0f, 0.0f, 0.0f}, 6.93f, "cloison est, vers le vestiaire"},
        {{0.0f, 0.0f, -1.0f}, 1.35f, "mur de facade sud"},
        {{0.0f, 1.0f, 0.0f}, 2.00f, "plafond a 3,60 m"},
        {{0.0f, -1.0f, 0.0f}, 1.60f, "sol"},
    };

    for (const Probe& probe : probes) {
        CAPTURE(probe.what);
        const physics::RayHit hit = world.raycast(middle, probe.direction, 16.0f);
        // La surface existe, et a la bonne distance : le generateur a bien ferme la piece.
        REQUIRE(hit.hit);
        CHECK(hit.distance == doctest::Approx(probe.distance).epsilon(0.02));
        // Et elle nous FAIT FACE : sa normale remonte le rayon. C'est l'assertion qui
        // porte tout le poids ici - une face retournee est touchee comme les autres, mais
        // rend une normale opposee, et disparait a l'ecran.
        CHECK(glm::dot(hit.normal, probe.direction) < -0.99f);
    }
}

TEST_CASE("Les cloisons du niveau ont une epaisseur") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    build(level, world);

    // La meme cloison, sondee de ses deux cotes : depuis le hall vers l'ouest, et depuis
    // le bureau vers l'est. L'ecart entre les deux faces EST l'epaisseur du mur.
    //
    // C'est le detail qui distingue un batiment d'un decor de theatre, parce qu'il se voit
    // partout ou le mur est perce : en franchissant une porte, on longe son tableau. Un
    // mur d'epaisseur nulle prive le regard de cette profondeur.
    const physics::RayHit fromHall =
        world.raycast(core::Vec3{0.0f, 1.6f, 1.5f}, core::Vec3{-1.0f, 0.0f, 0.0f}, 16.0f);
    const physics::RayHit fromOffice =
        world.raycast(core::Vec3{-10.0f, 1.6f, 1.5f}, core::Vec3{1.0f, 0.0f, 0.0f}, 16.0f);

    REQUIRE(fromHall.hit);
    REQUIRE(fromOffice.hit);
    // Le hall est a l'est de la cloison, le bureau a l'ouest : c'est donc la face
    // vue du hall qui a la plus grande abscisse.
    const core::f32 thickness = fromHall.point.x - fromOffice.point.x;
    CHECK(thickness == doctest::Approx(0.14f).epsilon(0.05));

    // Et les deux faces se tournent le dos : chacune regarde SA piece.
    CHECK(fromHall.normal.x > 0.99f);
    CHECK(fromOffice.normal.x < -0.99f);
}

TEST_CASE("Les ressauts de mur sont fermes") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    build(level, world);

    // Un mur de facade est plus epais qu'une cloison : 30 cm contre 14. La ou les deux se
    // rejoignent SUR LE MEME PLAN DE MUR, leurs faces sont decalees de 8 cm, et rien ne
    // les reliait. Le resultat etait une fente verticale du sol au plafond par laquelle
    // on voyait a travers le mur - le defaut signale apres essai.
    //
    // Le generateur ferme desormais chacun de ces douze ressauts par un retour. Les deux
    // rayons ci-dessous sont tires DANS l'ancienne fente : sans le retour, ils la
    // traversaient et allaient toucher bien plus loin.
    struct Probe {
        core::Vec3 origin;
        core::Vec3 direction;
        core::f32 distance;
        core::Vec3 normal;
        const char* what;
    };
    const Probe probes[] = {
        // Mur ouest du hall : cloison vers le bureau jusqu'a z = 7, facade au-dela.
        // Sans retour, ce rayon filait jusqu'au mur nord, a 2,93 m.
        {{-6.89f, 1.6f, 6.0f}, {0.0f, 0.0f, 1.0f}, 1.0f, {0.0f, 0.0f, -1.0f},
         "ressaut du mur ouest du hall, en z = 7"},
        // Mur sud de la chambre 1 : facade jusqu'a x = -7, cloison vers le hall au-dela.
        // Sans retour, ce rayon filait jusqu'au mur ouest, a 5,07 m.
        {{-5.0f, 1.6f, 9.11f}, {-1.0f, 0.0f, 0.0f}, 2.0f, {1.0f, 0.0f, 0.0f},
         "ressaut du mur sud de la chambre 1, en x = -7"},
    };

    for (const Probe& probe : probes) {
        CAPTURE(probe.what);
        const physics::RayHit hit = world.raycast(probe.origin, probe.direction, 12.0f);
        REQUIRE(hit.hit);
        CHECK(hit.distance == doctest::Approx(probe.distance).epsilon(0.05));
        CHECK(glm::dot(hit.normal, probe.normal) > 0.99f);
    }
}

TEST_CASE("Le plafond de l'atelier porte des poutres") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    build(level, world);

    // L'atelier occupe x de 2 a 14, z de 9 a 19, sous un plafond a 4,20 m. Trois poutres
    // le traversent, a x = 5, 8 et 11, retombant de 34 cm.
    //
    // Un plafond nu de cent metres carres n'existe pas, et surtout il ne donne au regard
    // aucune echelle - c'est l'une des raisons pour lesquelles une piece se lit comme une
    // boite. Ce test verifie que le relief est bien la, et qu'il n'est pas partout.
    const physics::RayHit onBeam =
        world.raycast(core::Vec3{8.0f, 1.6f, 14.0f}, core::Vec3{0.0f, 1.0f, 0.0f}, 8.0f);
    const physics::RayHit between =
        world.raycast(core::Vec3{6.5f, 1.6f, 14.0f}, core::Vec3{0.0f, 1.0f, 0.0f}, 8.0f);

    REQUIRE(onBeam.hit);
    REQUIRE(between.hit);
    CHECK(onBeam.distance == doctest::Approx(4.20f - 0.34f - 1.6f).epsilon(0.02));
    CHECK(between.distance == doctest::Approx(4.20f - 1.6f).epsilon(0.02));
    CHECK(onBeam.normal.y < -0.99f);
    CHECK(between.normal.y < -0.99f);
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

TEST_CASE("Un mur du niveau arrete le joueur") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    REQUIRE(world.create());
    const physics::CharacterHandle player = inhabit(level, world);
    REQUIRE(player != physics::kInvalidCharacter);

    walk(world, player, core::Vec3{0.0f, 0.0f, 0.0f}, 0.0f, 60); // se poser d'abord

    // Vers le sud : la facade est a z = 0, et sa face interieure a 15 cm de la. On marche
    // droit dessus pendant quatre secondes, soit quatre fois la distance a parcourir : ce
    // qui arrete le joueur ne peut etre que le mur.
    const core::Vec3 feet = walk(world, player, core::Vec3{0.0f, 0.0f, -1.0f}, 3.0f, 240);

    CHECK(feet.z > 0.15f);
    // Il doit s'arreter contre le mur, a un rayon pres - pas s'y enfoncer, pas rester
    // bloque a mi-chemin.
    CHECK(feet.z < 0.15f + kRadius + 0.2f);
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

    // Le hall s'ouvre sur le couloir par un passage large de 2,60 m, en z = 9, centre sur
    // x = 0. Un mur perce est la seule chose qui distingue un niveau d'une collection de
    // boites fermees : si le passage n'etait pas un vrai trou, le joueur buterait a z ~ 9.
    const core::Vec3 feet = walk(world, player, core::Vec3{0.0f, 0.0f, 1.0f}, 3.0f, 240);

    CHECK(feet.z > 11.0f);
    // Et il est passe SANS deriver : le couloir ne fait que 4 m de large.
    CHECK(feet.x > -1.5f);
    CHECK(feet.x < 1.5f);
    CHECK(world.characterOnGround(player));
}
