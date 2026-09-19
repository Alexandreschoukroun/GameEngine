#pragma once

#include "core/math.h"
#include "core/types.h"

#include <cmath>

namespace renderer {

// Nombre maximal de lumieres prises en compte dans une frame. C'est la valeur du budget
// de performance du SPEC : ~8 lumieres dynamiques visibles simultanement.
inline constexpr core::u32 kMaxLights = 8;

enum class LightType : core::u32 {
    // Rayonne dans toutes les directions depuis un point. Son ombre demanderait six cartes
    // de profondeur, une par face d'un cube : ce n'est pas fait aujourd'hui.
    Point = 0,
    // Cone oriente. Une seule carte de profondeur suffit, et c'est exactement ce dont la
    // lampe torche a besoin.
    Spot = 1,
};

struct Light {
    core::Vec3 position{0.0f, 0.0f, 0.0f};
    core::Vec3 color{1.0f, 1.0f, 1.0f};
    // Puissance. L'attenuation suivant 1/d^2, il faut des valeurs assez grandes : a 3 m,
    // l'intensite est deja divisee par 9.
    core::f32 intensity = 10.0f;

    LightType type = LightType::Point;

    // --- Specifique aux spots --------------------------------------------------------
    core::Vec3 direction{0.0f, -1.0f, 0.0f};
    // Demi-angles du cone. A l'interieur du premier, l'eclairage est plein ; entre les
    // deux, il s'eteint progressivement ; au-dela, rien. Sans ce degrade, le bord du cone
    // serait une decoupe nette et artificielle.
    core::f32 innerAngleRadians = core::radians(16.0f);
    core::f32 outerAngleRadians = core::radians(26.0f);
    // Portee, qui sert de plan lointain a la carte de profondeur. Trop grande, elle
    // gaspille la precision ; trop courte, elle coupe les ombres au loin.
    core::f32 range = 20.0f;

    // Une seule lumiere a ombre est geree pour l'instant : la premiere rencontree.
    bool castsShadow = false;
};

} // namespace renderer
