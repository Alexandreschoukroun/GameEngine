#pragma once

#include "core/types.h"

namespace core {

// Identifiant stable d'une entite, conserve dans les fichiers de scene.
//
// Tire au hasard sur 64 bits plutot qu'incremente : deux personnes qui creent des objets
// chacune de leur cote n'entrent jamais en collision, donc deux pieces faites separement
// peuvent fusionner sans renumeroter quoi que ce soit. Avec un compteur, elles utiliseraient
// toutes deux 1, 2, 3, et toute reference entre entites casserait a la fusion.
//
// Probabilite de collision : negligeable. Il faudrait environ 5 milliards d'entites pour
// atteindre une chance sur un milliard.
using Uuid = u64;

inline constexpr Uuid kInvalidUuid = 0;

Uuid generateUuid();

} // namespace core
