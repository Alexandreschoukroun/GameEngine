#pragma once

#include "core/math.h"
#include "core/types.h"

namespace renderer {

// Camera a la premiere personne : une position et deux angles.
//
// Pourquoi deux angles plutot qu'une orientation libre : un jeu first-person n'a jamais
// besoin de rouler sur le cote, et le couple yaw/pitch se borne facilement, ce qui evite
// que la camera se retourne (gimbal lock).
class Camera {
public:
    void setPerspective(core::f32 fovYRadians, core::f32 aspect, core::f32 nearZ,
                        core::f32 farZ);
    void setAspect(core::f32 aspect);

    void setPosition(const core::Vec3& position) { m_position = position; }
    const core::Vec3& position() const { return m_position; }

    // Angles en radians. Le pitch est borne a +/- 89 degres : a 90 exactement, la
    // direction de vue devient colinéaire a l'axe vertical et l'image bascule.
    void setRotation(core::f32 yawRadians, core::f32 pitchRadians);
    void addRotation(core::f32 yawDeltaRadians, core::f32 pitchDeltaRadians);

    core::f32 yaw() const { return m_yaw; }
    core::f32 pitch() const { return m_pitch; }

    // Repere local de la camera, deduit des angles.
    core::Vec3 forward() const;
    core::Vec3 right() const;

    core::Mat4 viewMatrix() const;
    core::Mat4 projectionMatrix() const;
    // Le produit envoye au shader : un seul uniforme au lieu de deux.
    core::Mat4 viewProjectionMatrix() const;

private:
    core::Vec3 m_position{0.0f, 0.0f, 0.0f};
    core::f32 m_yaw = 0.0f;
    core::f32 m_pitch = 0.0f;

    core::f32 m_fovY = core::radians(60.0f);
    core::f32 m_aspect = 16.0f / 9.0f;
    core::f32 m_nearZ = 0.05f;
    core::f32 m_farZ = 100.0f;
};

} // namespace renderer
