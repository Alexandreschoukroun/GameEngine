#pragma once

#include "scene/scene.h"

#include <vector>

namespace scene {

// Operations d'edition d'une scene : celles qu'un editeur declenche, et qu'aucun systeme
// de jeu n'a de raison d'appeler.
//
// Elles vivent dans la couche scene et non dans l'editeur, pour deux raisons. D'abord
// elles n'ont rien d'une affaire d'interface : dupliquer une entite est une operation sur
// des DONNEES. Ensuite elles sont ainsi testables sans fenetre - et elles en ont besoin,
// parce que leurs pieges ne se voient qu'apres coup.

// Duplique une entite ET ses descendants, puis rend la copie de la racine.
//
// Dupliquer les descendants n'est pas un supplement : c'est ce que l'on attend. Copier une
// porte sans sa poignee donnerait un objet incomplet, et personne ne penserait a copier la
// poignee separement.
//
// Trois choses ne sont deliberement PAS copiees :
//   - l'identifiant, qui doit rester unique - c'est la cle du fichier de scene ;
//   - la matrice monde, qui se recalcule ;
//   - tout ce qui n'existe qu'a l'execution (corps physique, voix audio), dont la copie
//     designerait la ressource de l'original.
//
// Rend kInvalidEntity si la source n'existe pas.
Entity duplicateEntity(Scene& scene, Entity source);

// Detruit une entite ET ses descendants.
//
// Detruire seulement l'entite laisserait ses enfants pointer sur un parent mort. La scene
// s'en remet - elle verifie la validite avant de suivre le lien - mais les enfants
// sauteraient a l'origine du monde, ce qui a l'air d'un bug et n'en est pas un.
void destroyEntityTree(Scene& scene, Entity entity);

// Descendants directs et indirects d'une entite, la racine exclue.
//
// Expose parce que l'editeur en a besoin pour afficher l'arbre, et qu'il n'y a aucune
// raison de le recalculer differemment a deux endroits. La recherche est un balayage : le
// lien de parente est stocke dans l'ENFANT, il n'existe donc aucune liste d'enfants.
void collectDescendants(const Scene& scene, Entity root, std::vector<Entity>& out);

} // namespace scene
