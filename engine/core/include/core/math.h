#pragma once

#include "core/types.h"

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// Vocabulaire mathematique du moteur.
//
// Ce sont des alias, pas des enveloppes : ce sont bien des types GLM qui circulent dans
// tout le moteur. C'est une exception assumee a la regle 2 du SPEC (pas de type tiers
// au-dessus de sa couche) : core est la couche la plus basse, donc tout le monde est
// au-dessus, et les maths sont un vocabulaire commun au meme titre que f32 ou u32.
// Les enveloppes auraient un cout de maintenance sans contrepartie.
//
// Conventions du moteur, valables partout :
//   - repere main droite, Y vers le haut ;
//   - la camera regarde vers -Z lorsque ses angles sont nuls ;
//   - profondeur dans [-1, +1] (convention OpenGL). Vulkan attendra [0, 1] : il faudra
//     alors definir GLM_FORCE_DEPTH_ZERO_TO_ONE, sinon l'image sort inversee.
namespace core {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;

inline constexpr f32 kPi = 3.14159265358979323846f;

inline constexpr f32 radians(f32 degrees) { return degrees * (kPi / 180.0f); }

} // namespace core
