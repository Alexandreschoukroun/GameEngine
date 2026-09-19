#pragma once

#include "core/math.h"
#include "core/types.h"

namespace renderer {

// Nombre maximal de lumieres prises en compte dans une frame. C'est la valeur du budget
// de performance du SPEC : ~8 lumieres dynamiques visibles simultanement, toutes avec
// ombres. Le shader declare un tableau de cette taille, fixee a la compilation.
inline constexpr core::u32 kMaxLights = 8;

// Lumiere ponctuelle : elle rayonne dans toutes les directions depuis un point.
// Le spot, avec son cone et son cookie de texture, viendra avec la lampe torche.
struct Light {
    core::Vec3 position{0.0f, 0.0f, 0.0f};
    core::Vec3 color{1.0f, 1.0f, 1.0f};
    // Puissance. L'attenuation suivant 1/d^2, il faut des valeurs assez grandes : a 3 m,
    // l'intensite est deja divisee par 9.
    core::f32 intensity = 10.0f;
};

} // namespace renderer
