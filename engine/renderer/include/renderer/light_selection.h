#pragma once

#include "renderer/light.h"

#include <span>

namespace renderer {

// Choisit les lumieres qui comptent pour ce point de vue.
//
// Le probleme qu'elle resout : le budget du SPEC est de huit lumieres simultanees, et le
// shader d'eclairage en boucle exactement huit. Un batiment de douze pieces en declare
// douze. Jusqu'ici, le jeu gardait simplement les PREMIERES rencontrees dans la scene -
// c'est-a-dire un ordre arbitraire, celui du registre. Resultat : quatre pieces au hasard
// n'etaient pas eclairees, et lesquelles pouvait changer d'un chargement a l'autre.
//
// Garder les premieres n'est pas un choix, c'est une absence de choix. Cette fonction en
// fait un : elle classe les lumieres par leur INFLUENCE au point de vue et remonte les
// meilleures en tete.
//
// L'influence vaut intensite / (1 + d^2), la meme decroissance que celle du shader, et
// vaut zero au-dela de la portee - une lumiere qui n'atteint pas l'observateur ne peut
// pas lui couter un emplacement.
//
// C'est une APPROXIMATION, et il faut connaitre son defaut : elle mesure la distance a
// l'observateur, pas a ce qu'il regarde. Une piece vivement eclairee, apercue de loin par
// une porte, peut donc s'eteindre alors qu'on la voit. Le vrai remede est un decoupage de
// l'ecran en tuiles, qui n'a de sens qu'avec beaucoup plus de lumieres que huit.
//
// Reordonne `lights` sur place et renvoie le nombre a conserver, toujours <= keep.
core::u32 selectStrongestLights(std::span<Light> lights, const core::Vec3& viewer,
                                core::u32 keep);

// L'influence d'une lumiere en un point. Exposee pour que le classement soit verifiable
// sans reproduire la formule dans le test.
core::f32 lightInfluence(const Light& light, const core::Vec3& point);

} // namespace renderer
