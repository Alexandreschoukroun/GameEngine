#pragma once

#include "core/math.h"
#include "core/types.h"
#include "scene/scene.h"

#include <vector>

namespace scene {

// Decoupage du niveau en volumes relies par des ouvertures.
//
// Trois systemes s'appuieront dessus, et c'est ce qui justifie de le poser des M3 :
//   - le rendu n'affichera que ce qui est atteignable depuis le secteur du joueur ;
//   - l'audio (M5) fera passer le son par les portails au lieu de traverser les murs ;
//   - l'IA (M7) entendra selon le meme chemin, donc un bruit derriere un mur portera moins
//     loin qu'a vol d'oiseau.

// Secteur : une piece, un couloir. Boite alignee sur les axes, centree sur la position du
// Transform - la position vit toujours dans le Transform, jamais en double.
//
// Une piece en L demande deux secteurs qui se recouvrent, et c'est tres bien ainsi : le
// decoupage est pose a la main, comme le prevoit le SPEC.
struct Sector {
    core::Vec3 halfExtents{1.0f, 1.0f, 1.0f};
};

// Portail : l'ouverture entre deux secteurs. Une porte, une arche, un trou dans un mur.
// Sa position vient aussi du Transform.
struct Portal {
    Entity sectorA = kInvalidEntity;
    Entity sectorB = kInvalidEntity;
    core::Vec3 halfExtents{0.5f, 1.0f, 0.1f};
};

// Secteur contenant ce point, ou kInvalidEntity. En cas de recouvrement, le premier
// trouve : deux secteurs qui se chevauchent decrivent la meme portion d'espace, le choix
// entre eux n'a donc pas d'importance pour l'appartenance.
Entity sectorAt(const Scene& scene, const core::Vec3& position);

// Secteurs atteignables depuis un secteur donne, en traversant au plus maxDepth portails.
// Le secteur de depart est toujours inclus.
//
// Parcours en largeur : les secteurs proches sortent avant les lointains, ce qui permettra
// a l'audio d'attenuer selon le nombre de portails traverses.
void reachableSectors(const Scene& scene, Entity from, core::u32 maxDepth,
                      std::vector<Entity>& out);

} // namespace scene
