#pragma once

#include "core/math.h"
#include "core/types.h"
#include "renderer/light.h"

namespace renderer {

class Camera;

// Lampe torche tenue a la main.
//
// Ce n'est pas un spot colle a la camera : une torche est decalee par rapport a l'oeil et
// accuse un RETARD quand on tourne la tete. Sans ce retard, le faisceau reste fige au
// centre de l'ecran et le cerveau n'y croit pas.
//
// Ne sont pas encore modelisees la batterie qui faiblit ni le tremblement lie a la marche :
// ils appartiennent a M8, avec l'ambiance et les options d'accessibilite - le SPEC exige
// que tout mouvement de camera soit desactivable.
class Flashlight {
public:
    // A appeler une fois par frame, avec le temps reel ecoule. La lampe rattrape la
    // camera d'une fraction de la distance restante.
    void update(const Camera& camera, core::f64 frameDeltaSeconds);

    // Place la lampe exactement sur la camera, sans transition : au demarrage, ou apres
    // une teleportation.
    void snapTo(const Camera& camera);

    void setEnabled(bool enabled) { m_enabled = enabled; }
    void toggle() { m_enabled = !m_enabled; }
    bool isEnabled() const { return m_enabled; }

    // La lumiere a donner au renderer. Intensite nulle si la lampe est eteinte.
    Light light() const;

private:
    core::Vec3 m_position{0.0f, 0.0f, 0.0f};
    core::Vec3 m_direction{0.0f, 0.0f, -1.0f};
    bool m_enabled = true;
};

} // namespace renderer
