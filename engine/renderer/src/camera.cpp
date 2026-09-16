#include "renderer/camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace renderer {
namespace {

// 89 degres et non 90 : a 90 exactement, forward() devient colineaire a l'axe vertical,
// le "haut" de la camera n'est plus defini et l'image bascule.
constexpr core::f32 kMaxPitch = core::radians(89.0f);
constexpr core::Vec3 kWorldUp{0.0f, 1.0f, 0.0f};

} // namespace

void Camera::setPerspective(core::f32 fovYRadians, core::f32 aspect, core::f32 nearZ,
                            core::f32 farZ) {
    m_fovY = fovYRadians;
    m_aspect = aspect;
    m_nearZ = nearZ;
    m_farZ = farZ;
}

void Camera::setAspect(core::f32 aspect) {
    if (aspect > 0.0f) {
        m_aspect = aspect;
    }
}

void Camera::setRotation(core::f32 yawRadians, core::f32 pitchRadians) {
    m_yaw = yawRadians;
    m_pitch = std::clamp(pitchRadians, -kMaxPitch, kMaxPitch);
}

void Camera::addRotation(core::f32 yawDeltaRadians, core::f32 pitchDeltaRadians) {
    setRotation(m_yaw + yawDeltaRadians, m_pitch + pitchDeltaRadians);
}

core::Vec3 Camera::forward() const {
    // Angles nuls => (0, 0, -1) : la camera regarde vers -Z, convention du moteur.
    const core::f32 cosPitch = std::cos(m_pitch);
    return core::Vec3{std::sin(m_yaw) * cosPitch, std::sin(m_pitch),
                      -std::cos(m_yaw) * cosPitch};
}

core::Vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), kWorldUp));
}

core::Mat4 Camera::viewMatrix() const {
    // lookAt exprime le monde depuis la camera : deplacer la camera d'un metre vers la
    // droite revient a deplacer le monde d'un metre vers la gauche.
    return glm::lookAt(m_position, m_position + forward(), kWorldUp);
}

core::Mat4 Camera::projectionMatrix() const {
    return glm::perspective(m_fovY, m_aspect, m_nearZ, m_farZ);
}

core::Mat4 Camera::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

} // namespace renderer
