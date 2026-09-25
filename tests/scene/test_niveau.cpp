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

#include <algorithm>
#include <array>
#include <cmath>
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
    "niveau_beton",           "niveau_bois",         "niveau_brique",
    "niveau_carrelage",       "niveau_carrelage_mural", "niveau_metal_rouille",
    "niveau_papier_peint",    "niveau_plancher",     "niveau_platre",
    "niveau_platre_peint",
};
constexpr std::size_t kLevelMeshCount = sizeof(kLevelMeshes) / sizeof(kLevelMeshes[0]);

// Les modeles que le JEU importe, et que la scene cite pour son mobilier, ses portes et
// ses caisses. Ils ne font pas partie du niveau : ils arrivent chacun avec leur propre
// matiere, declaree par leur fichier glTF.
//
// Ce tableau est le garde-fou : si le generateur pose un meuble dont le jeu n'importe pas
// le modele, le nom ne se resout pas et ce test le dit. Sans lui, le chargeur remplacerait
// silencieusement le maillage manquant par un substitut.
constexpr const char* kPropModels[] = {
    "caisse",   "loquet",  "porte_battant", "armoire", "banc",   "bureau_metal",
    "caisse_bois", "chaise", "chevet",     "etagere", "etau",   "lit",
    "table",    "tabouret", "tonneau",     "tuyaux",
    // Le luminaire et le desordre.
    "luminaire", "livres", "boite_outils", "cle", "seau", "bidon", "carton",
    "reveil",    "boite_conserve",
};
constexpr std::size_t kPropCount = sizeof(kPropModels) / sizeof(kPropModels[0]);

// Une geometrie de collision minimale mais VALIDE, pour les modeles de mobilier. Leur
// contenu n'a aucune importance ici : ce qu'on verifie a travers eux, ce sont les NOMS.
// Charger treize modeles pour verifier une orthographe serait payer cher une evidence.
const core::Vec3 kDummyPositions[3] = {
    {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
const core::u32 kDummyIndices[3] = {0, 1, 2};

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
    // Le cube, le loquet et les modeles de mobilier : le jeu les fabrique ou les
    // importe de son cote, et la scene les cite par leur nom logique.
    std::array<rhi::Mesh, kPropCount> props;
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

    // Le cube du jeu et les modeles importes. Chacun declare sa geometrie ET sa matiere
    // sous le meme nom logique : c'est la convention d'importModel, et elle evite d'avoir
    // deux listes a tenir.
    //
    // La collision suit la geometrie pour de bon : un meuble arrete le joueur, et c'est
    // ce que le test de circulation verifie plus bas.
    for (core::u32 i = 0; i < static_cast<core::u32>(kPropCount); ++i) {
        level.resources.addMesh(kPropModels[i], &level.props[i], scene::MeshBounds{});
        level.resources.addMaterial(kPropModels[i], scene::Material{});
        scene::CollisionMesh collision;
        collision.positions = kDummyPositions;
        collision.vertexCount = 3;
        collision.indices = kDummyIndices;
        collision.indexCount = 3;
        level.resources.addCollisionMesh(kPropModels[i], collision);
    }

    // Les matieres que le jeu charge depuis assets/textures/<nom>/.
    for (const char* material : {"beton", "bois", "brique", "carrelage", "carrelage_mural",
                                 "metal_rouille", "papier_peint", "plancher", "platre",
                                 "platre_peint", "porte"}) {
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
        // Un maillage de triangles est forcement statique, quoi que dise le fichier.
        CHECK(collider.isStatic);
        ++meshColliders;
    }
    // Le decor en compte un par matiere, et chaque meuble modelise y ajoute le sien : la
    // collision d'une chaise suit sa geometrie, faute de quoi elle serait decalee de la
    // moitie du meuble - l'origine d'un modele n'est pas son centre.
    CHECK(meshColliders > kLevelMeshCount);

    // Depuis que le niveau est meuble, les entites affichees ne se comptent plus : il y
    // a le decor, quinze portes, leurs poignees et des caisses, et en ajouter une est le
    // travail normal d'un auteur. C'est la PROPRIETE qui compte - toute entite affichee
    // cite un maillage et une matiere que le jeu connait - pas le nombre.
    core::u32 rendered = 0;
    for (auto [entity, renderer] :
         level.scene.registry().view<const scene::MeshRenderer>().each()) {
        (void)entity;
        CHECK(renderer.mesh != scene::kInvalidResource);
        CHECK(renderer.material != scene::kInvalidResource);
        ++rendered;
    }
    CHECK(rendered > kLevelMeshCount);

    // Le son des pas depend de la surface foulee : sans lui, le joueur marcherait en
    // silence sur un sol pourtant sonore.
    core::u32 surfaces = 0;
    for (auto [entity, surface] : level.scene.registry().view<const scene::Surface>().each()) {
        (void)entity;
        CHECK(surface.footstep != scene::kInvalidResource);
        ++surfaces;
    }
    CHECK(surfaces == kLevelMeshCount);

    // La scene ne declare AUCUNE lumiere : le batiment n'a plus de courant, et la lampe
    // torche est la seule source. Elle appartient au joueur, donc au jeu, pas a la scene -
    // c'est pour cela qu'on n'en trouve pas ici.
    //
    // Le controle tient donc a l'envers de ce qu'on ecrirait d'habitude : il garde le
    // CHOIX, et il tomberait si quelqu'un rallumait une lampe sans le vouloir.
    core::u32 lights = 0;
    for (auto [entity, light] :
         level.scene.registry().view<const scene::LightSource>().each()) {
        (void)entity;
        (void)light;
        ++lights;
    }
    CHECK(lights == 0);
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
        {{0.0f, 0.0f, -1.0f}, 1.43f, "mur de facade sud"},
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

TEST_CASE("Un mur reste d'aplomb sur toute sa longueur") {
    Level level;
    REQUIRE(loadLevel(level));
    physics::World world;
    build(level, world);

    // Un mur change de ROLE en cours de route : le mur ouest du hall separe le hall du
    // bureau jusqu'a z = 7, puis donne sur le vide au-dela. Tant que les deux roles
    // avaient des epaisseurs differentes, la face sautait de 8 cm a la jonction - ce qui
    // a d'abord ouvert une fente, puis, une fois la fente bouchee, laisse un decrochement
    // en plein milieu d'une surface plate. Les deux defauts ont la meme cause.
    //
    // La regle est donc devenue : la face visible est TOUJOURS au meme retrait. Ce test
    // la mesure directement - deux rayons de part et d'autre de chaque jonction doivent
    // toucher le meme plan, au millimetre.
    struct Pair {
        core::Vec3 first;
        core::Vec3 second;
        core::Vec3 direction;
        const char* what;
    };
    const Pair pairs[] = {
        // Mur ouest du hall, de part et d'autre de z = 7.
        {{0.0f, 1.6f, 4.5f}, {0.0f, 1.6f, 8.0f}, {-1.0f, 0.0f, 0.0f},
         "mur ouest du hall, jonction en z = 7"},
        // Mur sud de la chambre 1, de part et d'autre de x = -7.
        {{-6.0f, 1.6f, 12.0f}, {-8.5f, 1.6f, 12.0f}, {0.0f, 0.0f, -1.0f},
         "mur sud de la chambre 1, jonction en x = -7"},
    };

    for (const Pair& pair : pairs) {
        CAPTURE(pair.what);
        const physics::RayHit a = world.raycast(pair.first, pair.direction, 16.0f);
        const physics::RayHit b = world.raycast(pair.second, pair.direction, 16.0f);
        REQUIRE(a.hit);
        REQUIRE(b.hit);
        // Meme plan : la difference se mesure le long de la direction du rayon.
        const core::f32 drift = glm::dot(a.point - b.point, pair.direction);
        CHECK(std::abs(drift) < 0.005f);
        // Et les deux morceaux nous font face, comme le reste du mur.
        CHECK(glm::dot(a.normal, pair.direction) < -0.99f);
        CHECK(glm::dot(b.normal, pair.direction) < -0.99f);
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

TEST_CASE("Les luminaires restent en place, eteints et hors de portee") {
    Level level;
    REQUIRE(loadLevel(level));
    scene::Scene& scene = level.scene;
    scene.updateWorldTransforms();

    // Le courant est coupe, mais les suspensions restent : un batiment sans lampes au
    // plafond n'est pas sombre, il est vide, et la difference se voit des qu'on leve les
    // yeux.
    assets::MeshData lamp;
    REQUIRE(assets::loadGltfMesh(
        platform::assetPath("models/luminaire/hanging_industrial_lamp_1k.gltf").c_str(),
        lamp));
    core::Vec3 lampLow{0.0f, 0.0f, 0.0f};
    core::Vec3 lampHigh{0.0f, 0.0f, 0.0f};
    REQUIRE(lamp.computeBounds(lampLow, lampHigh));

    const scene::ResourceHandle luminaire = level.resources.findMesh("luminaire");
    REQUIRE(luminaire != scene::kInvalidResource);

    core::u32 fixtures = 0;
    for (auto [entity, renderer] :
         scene.registry().view<const scene::MeshRenderer>().each()) {
        if (renderer.mesh != luminaire) {
            continue;
        }
        ++fixtures;
        const core::Mat4 world = scene.worldMatrix(entity);
        const core::f32 scale = glm::length(core::Vec3(world[1]));

        // Le modele pend SOUS son origine. Son point le plus bas doit rester au-dessus de
        // la tete du joueur : un couloir de 2,80 m ne peut pas recevoir une suspension de
        // 1,34 m en entier, et le generateur la raccourcit pour cela. Sans ce controle, on
        // traverserait la lampe - ou pire, elle nous arreterait.
        const core::f32 bottom = core::Vec3(world[3]).y + lampLow.y * scale;
        CAPTURE(bottom);
        CHECK(bottom > 2.10f);

        // Et aucune n'a garde d'ampoule : une lampe eteinte qui eclaire serait le genre
        // d'incoherence qu'on ne remarque qu'au bout d'une heure.
        CHECK(scene.registry().try_get<scene::LightSource>(entity) == nullptr);
    }
    CHECK(fixtures == 14);
}

TEST_CASE("Le battant n'est pas une planche") {
    // Aucun catalogue libre n'offre de porte d'interieur : Poly Haven n'a qu'une porte de
    // chateau de 2,01 x 4,06 m avec son dormant, et ce qu'ambientCG appelle Door001 est
    // une MATIERE, pas un modele. Le battant est donc genere comme le reste du batiment.
    //
    // Une photo de porte sur un pave marche de face et se trahit de biais, parce que le
    // relief d'une porte est ce qui l'ombre. Ces verifications portent donc sur ce qu'une
    // texture ne peut pas donner : la profondeur et la silhouette.
    assets::MeshData leaf;
    REQUIRE(assets::loadGltfMesh(
        platform::assetPath("models/niveau/porte_battant.gltf").c_str(), leaf));

    core::Vec3 low{0.0f, 0.0f, 0.0f};
    core::Vec3 high{0.0f, 0.0f, 0.0f};
    REQUIRE(leaf.computeBounds(low, high));

    // Il occupe un cube d'unite centre sur l'origine, ce qui en fait un remplacement
    // DIRECT du cube du jeu : l'echelle de l'entite reste (largeur, hauteur, epaisseur),
    // et l'ancrage de la charniere reste -0,5.
    CHECK(low.x == doctest::Approx(-0.5f));
    CHECK(high.x == doctest::Approx(0.5f));
    CHECK(low.y == doctest::Approx(-0.5f));
    CHECK(high.y == doctest::Approx(0.5f));
    CHECK(low.z == doctest::Approx(-0.5f));
    CHECK(high.z == doctest::Approx(0.5f));

    // Sa face avant n'est PAS plane : les panneaux sont en retrait, et c'est ce retrait
    // qui fait l'ombre. Un pave n'aurait qu'une seule profondeur.
    core::f32 frontMost = -1.0f;
    core::f32 panelDepth = 1.0f;
    for (core::u32 i = 0; i < static_cast<core::u32>(leaf.positions.size()); ++i) {
        if (leaf.normals[i].z < 0.99f) {
            continue; // on ne regarde que ce qui fait face au visiteur
        }
        frontMost = std::max(frontMost, leaf.positions[i].z);
        panelDepth = std::min(panelDepth, leaf.positions[i].z);
    }
    CHECK(frontMost == doctest::Approx(0.5f));
    // Sur un battant de 6 cm, ce retrait vaut pres de deux centimetres.
    CHECK(frontMost - panelDepth > 0.2f);

    // Et la matiere couvre la face exactement une fois : la photo de porte doit tomber sur
    // les vrais panneaux, pas a cheval.
    core::Vec2 uvLow{1.0f, 1.0f};
    core::Vec2 uvHigh{0.0f, 0.0f};
    for (const core::Vec2& uv : leaf.uvs) {
        uvLow = core::Vec2(std::min(uvLow.x, uv.x), std::min(uvLow.y, uv.y));
        uvHigh = core::Vec2(std::max(uvHigh.x, uv.x), std::max(uvHigh.y, uv.y));
    }
    CHECK(uvLow.x == doctest::Approx(0.0f));
    CHECK(uvLow.y == doctest::Approx(0.0f));
    CHECK(uvHigh.x == doctest::Approx(1.0f));
    CHECK(uvHigh.y == doctest::Approx(1.0f));
}

TEST_CASE("Chaque baie recoit une porte battante, poignee comprise") {
    Level level;
    REQUIRE(loadLevel(level));

    // Le generateur pose un battant par embrasure a hauteur de porte, et aucun dans le
    // passage large qui ouvre le hall sur le couloir : une arche est une ouverture, pas
    // une baie.
    //
    // Rien de tout cela n'est du code neuf : la charniere, le frottement des gonds et la
    // saisie a la souris sont dans le moteur depuis M4. Ce test verifie seulement que les
    // battants sont poses comme ce mecanisme l'attend.
    core::u32 leaves = 0;
    for (auto [entity, hinge] : level.scene.registry().view<const scene::Hinge>().each()) {
        const auto* collider = level.scene.registry().try_get<scene::Collider>(entity);
        REQUIRE(collider != nullptr);
        // Un battant qui ne bouge pas n'est pas une porte. Et sa masse compte : a 1000
        // kg/m3 - la valeur par defaut de Jolt - il en pese 120 et ne s'ouvre plus a la
        // main.
        CHECK_FALSE(collider->isStatic);
        CHECK(collider->density == doctest::Approx(300.0f));
        // L'ancrage est dans le repere du battant, donc multiplie par son echelle : -0,5
        // tombe exactement sur son bord, la ou sont les gonds.
        CHECK(hinge.localAnchor.x == doctest::Approx(-0.5f));
        CHECK(hinge.axis.y == doctest::Approx(1.0f));
        // Elle s'ouvre d'un seul cote : deux butees symetriques donneraient un battant de
        // saloon.
        CHECK(hinge.minAngle < -1.0f);
        CHECK(hinge.maxAngle == doctest::Approx(0.0f));
        ++leaves;
    }
    CHECK(leaves == 15);

    // Chaque poignee est un ENFANT de son battant : elle n'a aucun code propre, elle suit
    // parce que la hierarchie de M3 le fait pour elle.
    scene::Scene& scene = level.scene;
    core::u32 handles = 0;
    for (auto [entity, parent] : scene.registry().view<const scene::Parent>().each()) {
        (void)entity;
        REQUIRE(scene.isValid(parent.value));
        CHECK(scene.registry().try_get<scene::Hinge>(parent.value) != nullptr);
        ++handles;
    }
    // DEUX par battant : une porte n'a jamais de poignee d'un seul cote. Elle
    // disparaissait des qu'on passait derriere, ce qui trahit le decor aussi surement
    // qu'un mur troue.
    CHECK(handles == leaves * 2);

    scene.updateWorldTransforms();
    for (auto [entity, parent] : scene.registry().view<const scene::Parent>().each()) {
        // Un enfant herite de l'echelle de son parent, et le battant est aplati a 6 cm.
        // L'echelle locale de la poignee ANNULE la sienne, sans quoi le modele serait
        // ecrase en plaque avec lui.
        const core::Mat4 world = scene.worldMatrix(entity);
        CHECK(glm::length(core::Vec3(world[0])) == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(glm::length(core::Vec3(world[1])) == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(glm::length(core::Vec3(world[2])) == doctest::Approx(1.0f).epsilon(0.01));

        // Et elle est du cote du bord LIBRE, a l'oppose des gonds : sans bras de levier,
        // on ne pourrait pas ouvrir.
        const core::Vec3 leaf(scene.worldMatrix(parent.value)[3]);
        const core::Vec3 handle(world[3]);
        CHECK(glm::length(handle - leaf) > 0.25f);
    }
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

    CHECK(feet.z > 0.07f);
    // Il doit s'arreter contre le mur, a un rayon pres - pas s'y enfoncer, pas rester
    // bloque a mi-chemin.
    CHECK(feet.z < 0.07f + kRadius + 0.2f);
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
