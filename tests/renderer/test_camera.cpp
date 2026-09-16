#include <doctest/doctest.h>

#include "renderer/camera.h"

#include <cmath>

namespace {

constexpr core::f32 kEpsilon = 1e-4f;

bool nearlyEqual(core::f32 lhs, core::f32 rhs) { return std::fabs(lhs - rhs) < kEpsilon; }

bool nearlyEqual(const core::Vec3& lhs, const core::Vec3& rhs) {
    return nearlyEqual(lhs.x, rhs.x) && nearlyEqual(lhs.y, rhs.y) && nearlyEqual(lhs.z, rhs.z);
}

} // namespace

TEST_CASE("Camera looks down -Z with zero angles") {
    const renderer::Camera camera;

    CHECK(nearlyEqual(camera.forward(), core::Vec3{0.0f, 0.0f, -1.0f}));
    CHECK(nearlyEqual(camera.right(), core::Vec3{1.0f, 0.0f, 0.0f}));
}

TEST_CASE("Camera yaw of +90 degrees turns to +X") {
    renderer::Camera camera;
    camera.setRotation(core::radians(90.0f), 0.0f);

    CHECK(nearlyEqual(camera.forward(), core::Vec3{1.0f, 0.0f, 0.0f}));
}

TEST_CASE("Camera pitch is clamped to avoid flipping over") {
    renderer::Camera camera;

    camera.setRotation(0.0f, core::radians(150.0f));
    CHECK(nearlyEqual(camera.pitch(), core::radians(89.0f)));

    camera.addRotation(0.0f, core::radians(-400.0f));
    CHECK(nearlyEqual(camera.pitch(), core::radians(-89.0f)));

    // Le vecteur haut reste defini : la camera ne bascule pas.
    CHECK(camera.forward().y < 0.0f);
}

TEST_CASE("View matrix brings the camera position back to the origin") {
    renderer::Camera camera;
    camera.setPosition(core::Vec3{3.0f, 2.0f, -5.0f});

    const core::Vec4 eyeInViewSpace = camera.viewMatrix() * core::Vec4{3.0f, 2.0f, -5.0f, 1.0f};

    CHECK(nearlyEqual(eyeInViewSpace.x, 0.0f));
    CHECK(nearlyEqual(eyeInViewSpace.y, 0.0f));
    CHECK(nearlyEqual(eyeInViewSpace.z, 0.0f));
}

TEST_CASE("A point straight ahead lands at the center of the screen") {
    renderer::Camera camera;
    camera.setPerspective(core::radians(60.0f), 16.0f / 9.0f, 0.05f, 100.0f);
    camera.setPosition(core::Vec3{0.0f, 0.0f, 0.0f});

    // 10 metres devant la camera, donc a -10 sur Z.
    const core::Vec4 clip = camera.viewProjectionMatrix() * core::Vec4{0.0f, 0.0f, -10.0f, 1.0f};
    REQUIRE(clip.w > 0.0f); // le point est bien devant la camera

    const core::f32 ndcX = clip.x / clip.w;
    const core::f32 ndcY = clip.y / clip.w;
    CHECK(nearlyEqual(ndcX, 0.0f));
    CHECK(nearlyEqual(ndcY, 0.0f));
}

TEST_CASE("A wider window does not stretch the vertical field of view") {
    renderer::Camera narrow;
    narrow.setPerspective(core::radians(60.0f), 1.0f, 0.05f, 100.0f);
    renderer::Camera wide;
    wide.setPerspective(core::radians(60.0f), 2.0f, 0.05f, 100.0f);

    const core::Vec4 point{1.0f, 1.0f, -5.0f, 1.0f};
    const core::Vec4 narrowClip = narrow.projectionMatrix() * point;
    const core::Vec4 wideClip = wide.projectionMatrix() * point;

    // Meme hauteur a l'ecran, mais le champ horizontal s'elargit : X retrecit.
    CHECK(nearlyEqual(narrowClip.y, wideClip.y));
    CHECK(wideClip.x < narrowClip.x);
}
