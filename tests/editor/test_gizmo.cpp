#include <doctest/doctest.h>

#include "editor/gizmo.h"
#include "renderer/camera.h"

namespace {

constexpr core::Vec2 kViewport{1280.0f, 720.0f};
constexpr core::Vec2 kCenter{640.0f, 360.0f};

// Une camera a 10 m de l'origine, regardant vers elle. C'est la situation ou l'on se
// trouve devant un objet qu'on veut deplacer.
renderer::Camera makeCamera() {
    renderer::Camera camera;
    camera.setPerspective(core::radians(60.0f), kViewport.x / kViewport.y, 0.05f, 100.0f);
    camera.setPosition(core::Vec3{0.0f, 0.0f, 10.0f});
    return camera;
}

} // namespace

TEST_CASE("The world projects onto the screen, and what is behind is refused") {
    const renderer::Camera camera = makeCamera();
    const core::Mat4 viewProjection = camera.viewProjectionMatrix();

    // L'origine est droit devant : elle tombe au centre de l'ecran.
    core::Vec2 screen{0.0f, 0.0f};
    REQUIRE(editor::worldToScreen(viewProjection, core::Vec3{0.0f, 0.0f, 0.0f}, kViewport,
                                  screen));
    CHECK(screen.x == doctest::Approx(kCenter.x).epsilon(0.01));
    CHECK(screen.y == doctest::Approx(kCenter.y).epsilon(0.01));

    // Vers la droite du monde : vers la droite de l'ecran. Vers le haut du monde : vers
    // le HAUT de l'ecran, donc un y plus petit - l'ecran compte vers le bas.
    core::Vec2 right{0.0f, 0.0f};
    core::Vec2 up{0.0f, 0.0f};
    REQUIRE(editor::worldToScreen(viewProjection, core::Vec3{1.0f, 0.0f, 0.0f}, kViewport, right));
    REQUIRE(editor::worldToScreen(viewProjection, core::Vec3{0.0f, 1.0f, 0.0f}, kViewport, up));
    CHECK(right.x > kCenter.x);
    CHECK(up.y < kCenter.y);

    // Derriere la camera, la division perspective rendrait une position parfaitement
    // plausible mais symetrique : une poignee apparaitrait a l'oppose de son objet.
    core::Vec2 behind{0.0f, 0.0f};
    CHECK_FALSE(editor::worldToScreen(viewProjection, core::Vec3{0.0f, 0.0f, 20.0f},
                                      kViewport, behind));
}

TEST_CASE("A screen ray starts at the eye and goes where the pixel points") {
    const renderer::Camera camera = makeCamera();
    const core::Mat4 inverse = glm::inverse(camera.viewProjectionMatrix());

    const editor::Ray center = editor::screenRay(inverse, kCenter, kViewport);
    // Le rayon du centre suit l'axe du regard : la camera est en +Z et regarde vers -Z.
    CHECK(center.direction.z < -0.99f);
    CHECK(std::abs(center.direction.x) < 0.01f);
    CHECK(std::abs(center.direction.y) < 0.01f);

    // Un pixel a droite donne un rayon qui part vers la droite.
    const editor::Ray right =
        editor::screenRay(inverse, core::Vec2{kCenter.x + 300.0f, kCenter.y}, kViewport);
    CHECK(right.direction.x > 0.1f);

    // Et la projection inverse est coherente avec la projection : le rayon du centre
    // repasse par l'origine du monde.
    const core::Vec3 toOrigin = -center.origin;
    const core::f32 t = glm::dot(toOrigin, center.direction);
    const core::Vec3 closest = center.origin + center.direction * t;
    CHECK(glm::length(closest) < 0.01f);
}

TEST_CASE("The closest point on an axis is found, unless it is seen edge-on") {
    // Un rayon qui descend droit sur l'axe X, a 3 m de l'origine : le point le plus
    // proche est evidemment x = 3.
    editor::Ray ray;
    ray.origin = core::Vec3{3.0f, 5.0f, 0.0f};
    ray.direction = core::Vec3{0.0f, -1.0f, 0.0f};

    core::f32 t = 0.0f;
    REQUIRE(editor::closestPointOnAxis(core::Vec3{0.0f, 0.0f, 0.0f},
                                       core::Vec3{1.0f, 0.0f, 0.0f}, ray, t));
    CHECK(t == doctest::Approx(3.0f));

    // L'axe decale : le resultat est relatif a SON origine, pas a celle du monde.
    REQUIRE(editor::closestPointOnAxis(core::Vec3{1.0f, 0.0f, 0.0f},
                                       core::Vec3{1.0f, 0.0f, 0.0f}, ray, t));
    CHECK(t == doctest::Approx(2.0f));

    // Rayon parallele a l'axe : on le regarde par la tranche, la solution part a
    // l'infini. Refuser est la seule reponse juste - un nombre enorme ferait bondir
    // l'objet a l'autre bout du niveau.
    ray.direction = core::Vec3{1.0f, 0.0f, 0.0f};
    CHECK_FALSE(editor::closestPointOnAxis(core::Vec3{0.0f, 0.0f, 0.0f},
                                           core::Vec3{1.0f, 0.0f, 0.0f}, ray, t));
}

TEST_CASE("Distance to a handle is measured to the segment, not to its line") {
    const core::Vec2 a{100.0f, 100.0f};
    const core::Vec2 b{200.0f, 100.0f};

    CHECK(editor::distanceToSegment(core::Vec2{150.0f, 110.0f}, a, b) == doctest::Approx(10.0f));
    // Au-dela du bout, c'est la distance a l'EXTREMITE qui compte. Mesurer a la droite
    // prolongee rendrait une poignee saisissable a l'autre bout de l'ecran.
    CHECK(editor::distanceToSegment(core::Vec2{300.0f, 100.0f}, a, b) == doctest::Approx(100.0f));
    // Segment degenere : les deux bouts confondus, c'est une distance a un point.
    CHECK(editor::distanceToSegment(core::Vec2{100.0f, 105.0f}, a, a) == doctest::Approx(5.0f));
}

TEST_CASE("Hovering a handle requires being near it") {
    const renderer::Camera camera = makeCamera();
    editor::TranslationGizmo gizmo;
    editor::TranslationGizmo::Frame frame;
    frame.viewProjection = camera.viewProjectionMatrix();
    frame.cameraPosition = camera.position();
    frame.viewport = kViewport;

    // Loin de tout : aucun axe.
    frame.mouse = core::Vec2{20.0f, 20.0f};
    gizmo.update(frame, core::Vec3{0.0f, 0.0f, 0.0f});
    CHECK(gizmo.hovered() == editor::GizmoAxis::None);

    // Sur le bras X, a mi-longueur. On projette le point pour savoir ou viser : c'est ce
    // que fait l'utilisateur avec ses yeux.
    const core::f32 arm = editor::TranslationGizmo::armLength(frame.cameraPosition,
                                                              core::Vec3{0.0f, 0.0f, 0.0f});
    core::Vec2 midX{0.0f, 0.0f};
    REQUIRE(editor::worldToScreen(frame.viewProjection, core::Vec3{arm * 0.5f, 0.0f, 0.0f},
                                  kViewport, midX));
    frame.mouse = midX;
    gizmo.update(frame, core::Vec3{0.0f, 0.0f, 0.0f});
    CHECK(gizmo.hovered() == editor::GizmoAxis::X);
}

TEST_CASE("Dragging a handle moves along its axis and nowhere else") {
    const renderer::Camera camera = makeCamera();
    editor::TranslationGizmo gizmo;
    editor::TranslationGizmo::Frame frame;
    frame.viewProjection = camera.viewProjectionMatrix();
    frame.cameraPosition = camera.position();
    frame.viewport = kViewport;

    const core::Vec3 origin{0.0f, 0.0f, 0.0f};
    const core::f32 arm = editor::TranslationGizmo::armLength(frame.cameraPosition, origin);
    core::Vec2 midX{0.0f, 0.0f};
    REQUIRE(editor::worldToScreen(frame.viewProjection, core::Vec3{arm * 0.5f, 0.0f, 0.0f},
                                  kViewport, midX));

    // Survol, puis appui : la saisie commence.
    frame.mouse = midX;
    gizmo.update(frame, origin);
    frame.mousePressed = true;
    frame.mouseHeld = true;
    const core::Vec3 atGrab = gizmo.update(frame, origin);
    CHECK(gizmo.isDragging());
    CHECK(gizmo.dragged() == editor::GizmoAxis::X);
    // Au moment meme de la saisie, rien ne bouge : on a attrape, on n'a pas tire.
    CHECK(glm::length(atGrab) < 0.001f);

    // On glisse vers la droite. Le deplacement doit etre POSITIF en X et nul ailleurs :
    // c'est toute la promesse d'un gizmo a axes.
    frame.mousePressed = false;
    frame.mouse = core::Vec2{midX.x + 120.0f, midX.y + 40.0f};
    const core::Vec3 delta = gizmo.update(frame, origin);
    CHECK(delta.x > 0.05f);
    CHECK(std::abs(delta.y) < 0.0001f);
    CHECK(std::abs(delta.z) < 0.0001f);

    // Relacher termine la saisie.
    frame.mouseHeld = false;
    gizmo.update(frame, origin);
    CHECK_FALSE(gizmo.isDragging());
}

TEST_CASE("Without a press there is no drag, however close the cursor") {
    const renderer::Camera camera = makeCamera();
    editor::TranslationGizmo gizmo;
    editor::TranslationGizmo::Frame frame;
    frame.viewProjection = camera.viewProjectionMatrix();
    frame.cameraPosition = camera.position();
    frame.viewport = kViewport;

    const core::Vec3 origin{0.0f, 0.0f, 0.0f};
    const core::f32 arm = editor::TranslationGizmo::armLength(frame.cameraPosition, origin);
    core::Vec2 midY{0.0f, 0.0f};
    REQUIRE(editor::worldToScreen(frame.viewProjection, core::Vec3{0.0f, arm * 0.5f, 0.0f},
                                  kViewport, midY));

    frame.mouse = midY;
    for (core::u32 i = 0; i < 10; ++i) {
        const core::Vec3 delta = gizmo.update(frame, origin);
        CHECK(glm::length(delta) == doctest::Approx(0.0f));
    }
    CHECK(gizmo.hovered() == editor::GizmoAxis::Y);
    CHECK_FALSE(gizmo.isDragging());
}

TEST_CASE("The gizmo keeps a constant apparent size") {
    // Deux fois plus loin, deux fois plus long dans le monde : a l'ecran, la taille ne
    // change pas. Un manipulateur qui retrecit quand on recule devient inutilisable.
    const core::Vec3 camera{0.0f, 0.0f, 10.0f};
    const core::f32 near = editor::TranslationGizmo::armLength(camera, core::Vec3{0.0f, 0.0f, 5.0f});
    const core::f32 far = editor::TranslationGizmo::armLength(camera, core::Vec3{0.0f, 0.0f, 0.0f});
    CHECK(far == doctest::Approx(near * 2.0f).epsilon(0.01));

    // Colle a la camera, un plancher evite que le gizmo ne disparaisse juste au moment
    // ou l'on veut s'en servir.
    CHECK(editor::TranslationGizmo::armLength(camera, camera) > 0.0f);
}
