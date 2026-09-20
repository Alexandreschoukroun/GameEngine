#pragma once

#include "editor/gizmo.h"
#include "scene/resource_table.h"
#include "scene/scene.h"

namespace editor {

// Designer un objet en cliquant dessus.
//
// Passer par la liste est acceptable avec vingt entites et impraticable avec deux cents :
// on sait ou est l'objet qu'on veut, pas son rang dans un arbre.

// Intersection d'un rayon et d'une boite alignee sur les axes, par la methode des
// tranches : on borne, pour chaque axe, l'intervalle de parcours du rayon a l'interieur
// de la boite, et l'on regarde si les trois intervalles se recouvrent.
//
// Rend la distance a l'entree. Vaut zero quand le rayon PART de l'interieur, cas qui se
// presente des qu'on clique alors qu'on est dans une piece - la boite du decor englobe
// alors la camera.
bool rayIntersectsAabb(const Ray& ray, const core::Vec3& min, const core::Vec3& max,
                       core::f32& outDistance);

// Intersection d'un rayon et d'une sphere. Sert aux entites sans geometrie - lumieres,
// sources sonores, secteurs - qui doivent rester designables alors qu'elles n'ont aucune
// etendue a montrer.
bool rayIntersectsSphere(const Ray& ray, const core::Vec3& center, core::f32 radius,
                         core::f32& outDistance);

// Entite la plus proche sous le rayon, ou kInvalidEntity.
//
// Les entites affichees sont testees contre l'encombrement de leur maillage, transforme
// dans le monde ; les autres contre une petite sphere posee sur leur origine. La scene
// doit avoir ses matrices monde a jour.
scene::Entity pickEntity(const scene::Scene& scene, const scene::ResourceTable& resources,
                         const Ray& ray);

} // namespace editor
