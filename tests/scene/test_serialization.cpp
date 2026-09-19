#include <doctest/doctest.h>

#include "scene/resource_table.h"
#include "scene/scene.h"
#include "scene/serialization.h"

namespace {

// Les entites recoivent un identifiant aleatoire a la creation. Pour comparer deux scenes
// construites dans des ordres differents, on impose les memes identifiants a la main.
scene::Entity makeEntity(scene::Scene& scene, const char* name, core::Uuid id,
                         const core::Vec3& position) {
    const scene::Entity entity = scene.createEntity(name);
    scene.registry().get<scene::Id>(entity).value = id;
    scene.registry().get<scene::Transform>(entity).position = position;
    return entity;
}

} // namespace

TEST_CASE("Saving the same scene twice produces the same bytes") {
    scene::Scene scene;
    const scene::ResourceTable resources;

    makeEntity(scene, "mur", 0x1111, core::Vec3{1.0f, 2.0f, 3.0f});
    makeEntity(scene, "lampe", 0x2222, core::Vec3{-4.5f, 0.25f, 0.0f});

    CHECK(scene::saveSceneToString(scene, resources) ==
          scene::saveSceneToString(scene, resources));
}

TEST_CASE("Creation order does not change the output") {
    scene::Scene first;
    scene::Scene second;
    const scene::ResourceTable resources;

    // Memes entites, ordres de creation inverses : le registre les parcourra
    // differemment, mais le tri par identifiant doit effacer cette difference.
    makeEntity(first, "a", 0x00aa, core::Vec3{1.0f, 0.0f, 0.0f});
    makeEntity(first, "b", 0x00bb, core::Vec3{0.0f, 1.0f, 0.0f});

    makeEntity(second, "b", 0x00bb, core::Vec3{0.0f, 1.0f, 0.0f});
    makeEntity(second, "a", 0x00aa, core::Vec3{1.0f, 0.0f, 0.0f});

    CHECK(scene::saveSceneToString(first, resources) ==
          scene::saveSceneToString(second, resources));
}

TEST_CASE("A parent is referenced by its stable id, not by its position in the file") {
    scene::Scene scene;
    const scene::ResourceTable resources;

    const scene::Entity parent = makeEntity(scene, "porte", 0xf000, core::Vec3{0.0f, 0.0f, 0.0f});
    const scene::Entity child = makeEntity(scene, "poignee", 0x0001, core::Vec3{0.4f, 0.0f, 0.0f});
    REQUIRE(scene.setParent(child, parent));

    const std::string json = scene::saveSceneToString(scene, resources);

    // L'enfant a le plus petit identifiant : il est ecrit AVANT son parent. Un format qui
    // designerait le parent par son rang dans le fichier serait deja casse ici.
    CHECK(json.find("\"parent\": \"0x000000000000f000\"") != std::string::npos);
    CHECK(json.find("\"id\": \"0x0000000000000001\"") < json.find("\"id\": \"0x000000000000f000\""));
}

TEST_CASE("Floats are rounded so that diffs stay readable") {
    scene::Scene scene;
    const scene::ResourceTable resources;

    // 0.35f vaut 0.34999999403953552 en double : ecrit brut, il polluerait chaque diff.
    makeEntity(scene, "objet", 0x0042, core::Vec3{0.35f, -0.0f, 2.5f});

    const std::string json = scene::saveSceneToString(scene, resources);

    CHECK(json.find("0.35") != std::string::npos);
    CHECK(json.find("0.34999") == std::string::npos);
    // Le zero negatif est normalise : "-0.0" dans un fichier serait un faux changement.
    CHECK(json.find("-0.0") == std::string::npos);
}

TEST_CASE("The format carries a version number") {
    const scene::Scene scene;
    const scene::ResourceTable resources;

    CHECK(scene::saveSceneToString(scene, resources).find("\"version\": 1") !=
          std::string::npos);
}

TEST_CASE("Saving, loading and saving again gives the same file") {
    scene::Scene original;
    const scene::ResourceTable resources;

    const scene::Entity parent = makeEntity(original, "porte", 0xaaaa, core::Vec3{1.0f, 0.0f, 2.0f});
    const scene::Entity child = makeEntity(original, "poignee", 0x0001, core::Vec3{0.4f, 1.1f, 0.0f});
    REQUIRE(original.setParent(child, parent));
    original.registry().emplace<scene::LightSource>(
        parent, scene::LightSource{core::Vec3{1.0f, 0.5f, 0.25f}, 12.0f});

    const std::string saved = scene::saveSceneToString(original, resources);

    scene::Scene reloaded;
    REQUIRE(scene::loadSceneFromString(reloaded, resources, saved));

    // L'aller-retour complet doit etre neutre : c'est ce qui garantit qu'ouvrir un niveau
    // dans l'editeur et le resauvegarder sans rien toucher ne produit aucun diff.
    CHECK(scene::saveSceneToString(reloaded, resources) == saved);
}

TEST_CASE("A child written before its parent is still linked") {
    scene::Scene original;
    const scene::ResourceTable resources;

    // L'enfant a le plus petit identifiant : il apparait avant son parent dans le fichier.
    const scene::Entity parent = makeEntity(original, "parent", 0xff00, core::Vec3{});
    const scene::Entity child = makeEntity(original, "enfant", 0x0002, core::Vec3{});
    REQUIRE(original.setParent(child, parent));

    scene::Scene reloaded;
    REQUIRE(scene::loadSceneFromString(reloaded, resources,
                                       scene::saveSceneToString(original, resources)));

    const scene::Entity loadedChild = reloaded.findByName("enfant");
    REQUIRE(loadedChild != scene::kInvalidEntity);
    const auto* link = reloaded.registry().try_get<scene::Parent>(loadedChild);
    REQUIRE(link != nullptr);
    CHECK(reloaded.registry().get<scene::Id>(link->value).value == 0xff00);
}

TEST_CASE("An unknown format version is refused") {
    scene::Scene scene;
    const scene::ResourceTable resources;

    CHECK_FALSE(scene::loadSceneFromString(scene, resources,
                                           R"({"version": 99, "entities": []})"));
}

TEST_CASE("Malformed JSON is refused without throwing") {
    scene::Scene scene;
    const scene::ResourceTable resources;

    CHECK_FALSE(scene::loadSceneFromString(scene, resources, "{ ceci n est pas du json"));
}

TEST_CASE("A failed load leaves the previous scene untouched") {
    scene::Scene scene;
    const scene::ResourceTable resources;
    makeEntity(scene, "survivant", 0x1234, core::Vec3{});

    // Parent inexistant : la lecture echoue en seconde passe, donc apres avoir deja cree
    // des entites dans la scene temporaire.
    const char* broken = R"({"version": 1, "entities": [
        {"id": "0x0005", "name": "orphelin", "parent": "0x9999"}]})";
    CHECK_FALSE(scene::loadSceneFromString(scene, resources, broken));

    // Tout ou rien : la scene d'origine doit etre intacte.
    CHECK(scene.findByName("survivant") != scene::kInvalidEntity);
    CHECK(scene.findByName("orphelin") == scene::kInvalidEntity);
}
