#include <doctest/doctest.h>

#include "editor/picking.h"
#include "scene/components.h"

namespace {

editor::Ray rayTowards(const core::Vec3& from, const core::Vec3& to) {
    editor::Ray ray;
    ray.origin = from;
    ray.direction = glm::normalize(to - from);
    return ray;
}

// Un cube unite enregistre avec son encombrement. Le maillage GPU est nul : la
// designation ne regarde que la boite, jamais les triangles.
scene::ResourceHandle addUnitCube(scene::ResourceTable& resources) {
    scene::MeshBounds bounds;
    bounds.min = core::Vec3{-0.5f, -0.5f, -0.5f};
    bounds.max = core::Vec3{0.5f, 0.5f, 0.5f};
    bounds.valid = true;
    // addMesh refuse un maillage nul : on passe une adresse factice, jamais dereferencee
    // par la designation.
    static int dummy = 0;
    return resources.addMesh("cube", reinterpret_cast<const rhi::Mesh*>(&dummy), bounds);
}

} // namespace

TEST_CASE("A ray meets an axis-aligned box, or misses it") {
    const core::Vec3 min{-1.0f, -1.0f, -1.0f};
    const core::Vec3 max{1.0f, 1.0f, 1.0f};
    core::f32 distance = -1.0f;

    // Droit dessus, depuis 5 m : la face est a 4 m.
    editor::Ray ray = rayTowards(core::Vec3{0.0f, 0.0f, 5.0f}, core::Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(editor::rayIntersectsAabb(ray, min, max, distance));
    CHECK(distance == doctest::Approx(4.0f));

    // A cote : la boite ne fait qu'un metre de demi-cote.
    ray = rayTowards(core::Vec3{5.0f, 0.0f, 5.0f}, core::Vec3{5.0f, 0.0f, 0.0f});
    CHECK_FALSE(editor::rayIntersectsAabb(ray, min, max, distance));

    // Depuis l'INTERIEUR : la distance utile est nulle. Ce cas se presente des qu'on
    // clique en etant dans une piece - la boite du decor englobe alors la camera.
    ray = rayTowards(core::Vec3{0.0f, 0.0f, 0.0f}, core::Vec3{0.0f, 0.0f, -1.0f});
    REQUIRE(editor::rayIntersectsAabb(ray, min, max, distance));
    CHECK(distance == doctest::Approx(0.0f));

    // Derriere soi : on ne designe pas ce qu'on ne voit pas.
    ray = rayTowards(core::Vec3{0.0f, 0.0f, 5.0f}, core::Vec3{0.0f, 0.0f, 10.0f});
    CHECK_FALSE(editor::rayIntersectsAabb(ray, min, max, distance));
}

TEST_CASE("A ray parallel to a face is handled without dividing by zero") {
    const core::Vec3 min{-1.0f, -1.0f, -1.0f};
    const core::Vec3 max{1.0f, 1.0f, 1.0f};
    core::f32 distance = -1.0f;

    // Parallele a l'axe X, et a la hauteur de la boite : il la traverse.
    editor::Ray ray;
    ray.origin = core::Vec3{-5.0f, 0.0f, 0.0f};
    ray.direction = core::Vec3{1.0f, 0.0f, 0.0f};
    REQUIRE(editor::rayIntersectsAabb(ray, min, max, distance));
    CHECK(distance == doctest::Approx(4.0f));

    // Parallele mais trop haut : il passe au-dessus sans jamais entrer. Sans le cas
    // particulier, la division par une direction nulle donnerait un infini.
    ray.origin = core::Vec3{-5.0f, 3.0f, 0.0f};
    CHECK_FALSE(editor::rayIntersectsAabb(ray, min, max, distance));
}

TEST_CASE("A ray meets a sphere, or misses it") {
    core::f32 distance = -1.0f;
    const core::Vec3 center{0.0f, 0.0f, 0.0f};

    editor::Ray ray = rayTowards(core::Vec3{0.0f, 0.0f, 5.0f}, center);
    REQUIRE(editor::rayIntersectsSphere(ray, center, 1.0f, distance));
    CHECK(distance == doctest::Approx(4.0f));

    // Frolee de trop loin.
    ray.origin = core::Vec3{3.0f, 0.0f, 5.0f};
    ray.direction = core::Vec3{0.0f, 0.0f, -1.0f};
    CHECK_FALSE(editor::rayIntersectsSphere(ray, center, 1.0f, distance));

    // Entierement derriere : on ne selectionne pas ce qu'on ne voit pas.
    ray.origin = core::Vec3{0.0f, 0.0f, 5.0f};
    ray.direction = core::Vec3{0.0f, 0.0f, 1.0f};
    CHECK_FALSE(editor::rayIntersectsSphere(ray, center, 1.0f, distance));
}

TEST_CASE("Clicking picks the nearest entity under the ray") {
    scene::ResourceTable resources;
    const scene::ResourceHandle cube = addUnitCube(resources);
    scene::Scene scene;

    const scene::Entity near = scene.createEntity("proche");
    scene.registry().get<scene::Transform>(near).position = core::Vec3{0.0f, 0.0f, 0.0f};
    scene.registry().emplace<scene::MeshRenderer>(near, scene::MeshRenderer{cube, 0});

    const scene::Entity far = scene.createEntity("loin");
    scene.registry().get<scene::Transform>(far).position = core::Vec3{0.0f, 0.0f, -5.0f};
    scene.registry().emplace<scene::MeshRenderer>(far, scene::MeshRenderer{cube, 0});

    scene.updateWorldTransforms();

    // Les deux sont sur le trajet : c'est le PLUS PROCHE qui doit sortir, sans quoi on
    // selectionnerait a travers les murs.
    const editor::Ray ray = rayTowards(core::Vec3{0.0f, 0.0f, 8.0f}, core::Vec3{0.0f, 0.0f, 0.0f});
    CHECK(editor::pickEntity(scene, resources, ray) == near);

    // A cote de tout : rien.
    const editor::Ray miss =
        rayTowards(core::Vec3{20.0f, 0.0f, 8.0f}, core::Vec3{20.0f, 0.0f, 0.0f});
    CHECK(editor::pickEntity(scene, resources, miss) == scene::kInvalidEntity);
}

TEST_CASE("An entity follows its transform when picked") {
    scene::ResourceTable resources;
    const scene::ResourceHandle cube = addUnitCube(resources);
    scene::Scene scene;

    const scene::Entity moved = scene.createEntity("deplace");
    scene.registry().get<scene::Transform>(moved).position = core::Vec3{4.0f, 0.0f, 0.0f};
    scene.registry().emplace<scene::MeshRenderer>(moved, scene::MeshRenderer{cube, 0});
    scene.updateWorldTransforms();

    // A l'origine, il n'y a plus rien.
    CHECK(editor::pickEntity(scene, resources,
                             rayTowards(core::Vec3{0.0f, 0.0f, 8.0f},
                                        core::Vec3{0.0f, 0.0f, 0.0f})) ==
          scene::kInvalidEntity);
    // La ou il est, oui. L'encombrement suit donc bien la matrice monde.
    CHECK(editor::pickEntity(scene, resources,
                             rayTowards(core::Vec3{4.0f, 0.0f, 8.0f},
                                        core::Vec3{4.0f, 0.0f, 0.0f})) == moved);
}

TEST_CASE("Entities without geometry stay selectable") {
    scene::ResourceTable resources;
    scene::Scene scene;

    // Une lumiere n'a rien a montrer, et il faut pourtant pouvoir la designer : c'est
    // souvent l'objet le plus difficile a retrouver dans une liste.
    const scene::Entity light = scene.createEntity("lampe");
    scene.registry().get<scene::Transform>(light).position = core::Vec3{0.0f, 2.0f, 0.0f};
    scene.registry().emplace<scene::LightSource>(light, scene::LightSource{});
    scene.updateWorldTransforms();

    CHECK(editor::pickEntity(scene, resources,
                             rayTowards(core::Vec3{0.0f, 2.0f, 6.0f},
                                        core::Vec3{0.0f, 2.0f, 0.0f})) == light);
    // Mais sa sphere reste petite : a un metre de cote, on ne l'attrape plus.
    CHECK(editor::pickEntity(scene, resources,
                             rayTowards(core::Vec3{1.0f, 2.0f, 6.0f},
                                        core::Vec3{1.0f, 2.0f, 0.0f})) ==
          scene::kInvalidEntity);
}

TEST_CASE("A scaled entity is picked over its whole extent") {
    scene::ResourceTable resources;
    const scene::ResourceHandle cube = addUnitCube(resources);
    scene::Scene scene;

    const scene::Entity wall = scene.createEntity("mur");
    // Un cube unite etire en panneau de 6 m de large.
    scene.registry().get<scene::Transform>(wall).scale = core::Vec3{6.0f, 3.0f, 0.2f};
    scene.registry().emplace<scene::MeshRenderer>(wall, scene::MeshRenderer{cube, 0});
    scene.updateWorldTransforms();

    // A 2,5 m du centre : hors du cube d'origine, dans le panneau etire. L'echelle doit
    // donc etre prise en compte, sinon seuls les objets a taille un seraient designables.
    CHECK(editor::pickEntity(scene, resources,
                             rayTowards(core::Vec3{2.5f, 0.0f, 6.0f},
                                        core::Vec3{2.5f, 0.0f, 0.0f})) == wall);
    // Au-dela du bord, plus rien.
    CHECK(editor::pickEntity(scene, resources,
                             rayTowards(core::Vec3{4.0f, 0.0f, 6.0f},
                                        core::Vec3{4.0f, 0.0f, 0.0f})) ==
          scene::kInvalidEntity);
}
