#include "editor/gizmo.h"

#include <algorithm>
#include <cmath>

namespace editor {
namespace {

// Rayon, en pixels, autour d'un bras pour qu'il se considere survole. Assez large pour
// qu'on l'attrape sans viser au pixel, assez etroit pour que deux bras voisins restent
// distinguables.
constexpr core::f32 kHandleRadiusPixels = 10.0f;

// Longueur d'un bras, en fraction de la distance a la camera. Environ un sixieme donne un
// gizmo qui occupe une part constante et confortable de l'ecran.
constexpr core::f32 kArmFraction = 0.16f;

// En deca, l'axe et le rayon sont trop paralleles pour que leur point le plus proche ait
// un sens : la solution part a l'infini.
constexpr core::f32 kParallelEpsilon = 1e-4f;

} // namespace

bool worldToScreen(const core::Mat4& viewProjection, const core::Vec3& world,
                   const core::Vec2& viewport, core::Vec2& outScreen) {
    const core::Vec4 clip = viewProjection * core::Vec4(world, 1.0f);
    // w est la profondeur devant la camera. Negatif ou nul : le point est derriere l'oeil,
    // et la division qui suit rendrait des coordonnees plausibles mais symetriques.
    if (clip.w <= kParallelEpsilon) {
        return false;
    }
    const core::Vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
    // De [-1, 1] vers les pixels. Y est inverse : l'ecran compte vers le bas, l'espace
    // normalise vers le haut.
    outScreen = core::Vec2((ndc.x * 0.5f + 0.5f) * viewport.x,
                           (1.0f - (ndc.y * 0.5f + 0.5f)) * viewport.y);
    return true;
}

Ray screenRay(const core::Mat4& inverseViewProjection, const core::Vec2& screen,
              const core::Vec2& viewport) {
    const core::f32 x = (screen.x / viewport.x) * 2.0f - 1.0f;
    const core::f32 y = 1.0f - (screen.y / viewport.y) * 2.0f;

    // Deux points du meme pixel, l'un au plan proche, l'autre au plan lointain : leur
    // difference donne la direction. C'est plus robuste que de reconstruire l'oeil et le
    // champ de vision separement, et ca marche pour une projection orthographique aussi.
    const core::Vec4 nearPoint = inverseViewProjection * core::Vec4(x, y, -1.0f, 1.0f);
    const core::Vec4 farPoint = inverseViewProjection * core::Vec4(x, y, 1.0f, 1.0f);

    Ray ray;
    if (std::abs(nearPoint.w) <= kParallelEpsilon || std::abs(farPoint.w) <= kParallelEpsilon) {
        return ray;
    }
    ray.origin = core::Vec3(nearPoint) / nearPoint.w;
    const core::Vec3 target = core::Vec3(farPoint) / farPoint.w;
    const core::Vec3 delta = target - ray.origin;
    const core::f32 length = glm::length(delta);
    ray.direction = length > kParallelEpsilon ? delta / length : core::Vec3(0.0f, 0.0f, -1.0f);
    return ray;
}

bool closestPointOnAxis(const core::Vec3& axisOrigin, const core::Vec3& axisDirection,
                        const Ray& ray, core::f32& outT) {
    // Deux droites gauches dans l'espace : on cherche le point de la premiere le plus
    // proche de la seconde. Le systeme se ramene a deux equations, dont le determinant
    // vaut 1 - (u.v)^2 - nul exactement quand elles sont paralleles.
    const core::Vec3 u = glm::normalize(axisDirection);
    const core::Vec3 v = ray.direction;
    const core::f32 uv = glm::dot(u, v);
    const core::f32 determinant = 1.0f - uv * uv;
    if (determinant < kParallelEpsilon) {
        return false;
    }

    const core::Vec3 w = axisOrigin - ray.origin;
    outT = (uv * glm::dot(w, v) - glm::dot(w, u)) / determinant;
    return true;
}

core::f32 distanceToSegment(const core::Vec2& point, const core::Vec2& a,
                            const core::Vec2& b) {
    const core::Vec2 segment = b - a;
    const core::f32 lengthSquared = glm::dot(segment, segment);
    if (lengthSquared <= kParallelEpsilon) {
        return glm::length(point - a);
    }
    // Projection bornee a [0, 1] : au-dela des extremites, c'est la distance au bout du
    // segment qui compte, pas a la droite qui le prolonge.
    const core::f32 t =
        std::clamp(glm::dot(point - a, segment) / lengthSquared, 0.0f, 1.0f);
    return glm::length(point - (a + segment * t));
}

core::f32 TranslationGizmo::armLength(const core::Vec3& cameraPosition,
                                      const core::Vec3& origin) {
    const core::f32 distance = glm::length(origin - cameraPosition);
    // Un plancher : colle a l'objet, la distance tend vers zero et le gizmo
    // disparaitrait juste au moment ou l'on veut s'en servir.
    return std::max(distance * kArmFraction, 0.05f);
}

core::Vec3 TranslationGizmo::axisDirection(GizmoAxis axis) {
    switch (axis) {
        case GizmoAxis::X: return core::Vec3(1.0f, 0.0f, 0.0f);
        case GizmoAxis::Y: return core::Vec3(0.0f, 1.0f, 0.0f);
        case GizmoAxis::Z: return core::Vec3(0.0f, 0.0f, 1.0f);
        case GizmoAxis::None:
        default: return core::Vec3(0.0f, 0.0f, 0.0f);
    }
}

core::Vec3 TranslationGizmo::update(const Frame& frame, const core::Vec3& origin) {
    // Relacher termine la saisie, quoi qu'il arrive par ailleurs. On le traite en premier
    // pour qu'un bouton relache hors de la fenetre ne laisse pas un gizmo accroche.
    if (!frame.mouseHeld) {
        m_dragged = GizmoAxis::None;
    }

    const core::f32 arm = armLength(frame.cameraPosition, origin);
    const core::Mat4 inverseViewProjection = glm::inverse(frame.viewProjection);
    const Ray ray = screenRay(inverseViewProjection, frame.mouse, frame.viewport);

    // --- survol ---------------------------------------------------------------------
    //
    // On ne recalcule pas le survol pendant une saisie : le curseur s'eloigne forcement
    // du bras quand on tire dessus, et perdre l'axe en cours de geste serait absurde.
    if (!isDragging()) {
        m_hovered = GizmoAxis::None;
        core::Vec2 originScreen;
        if (worldToScreen(frame.viewProjection, origin, frame.viewport, originScreen)) {
            core::f32 best = kHandleRadiusPixels;
            for (const GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
                core::Vec2 tipScreen;
                const core::Vec3 tip = origin + axisDirection(axis) * arm;
                if (!worldToScreen(frame.viewProjection, tip, frame.viewport, tipScreen)) {
                    continue;
                }
                const core::f32 distance =
                    distanceToSegment(frame.mouse, originScreen, tipScreen);
                // Strictement inferieur : a egalite, le premier axe teste gagne, ce qui
                // reste deterministe.
                if (distance < best) {
                    best = distance;
                    m_hovered = axis;
                }
            }
        }
    }

    // --- saisie ---------------------------------------------------------------------
    if (frame.mousePressed && m_hovered != GizmoAxis::None && !isDragging()) {
        core::f32 t = 0.0f;
        if (closestPointOnAxis(origin, axisDirection(m_hovered), ray, t)) {
            m_dragged = m_hovered;
            // On memorise OU l'on a attrape le bras. Sans ca, l'objet sauterait pour
            // centrer son origine sous le curseur des la premiere image.
            m_grabOffset = t;
        }
    }

    if (!isDragging()) {
        return core::Vec3(0.0f);
    }

    const core::Vec3 direction = axisDirection(m_dragged);
    core::f32 t = 0.0f;
    if (!closestPointOnAxis(origin, direction, ray, t)) {
        // L'axe est devenu parallele au regard en cours de geste : on ne bouge pas, mais
        // on garde la saisie - le joueur peut revenir a un angle exploitable.
        return core::Vec3(0.0f);
    }
    // L'ecart au point de saisie, et non la position absolue : l'origine ayant bouge de
    // ce qu'on a rendu a l'image precedente, l'ecart retombe naturellement a zero.
    return direction * (t - m_grabOffset);
}

} // namespace editor
