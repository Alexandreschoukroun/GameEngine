#pragma once

#include "scene/resource_table.h"
#include "scene/scene.h"

// En-tete INTERNE a l'editeur : il vit dans src/ et non dans include/, donc rien
// au-dessus de cette couche ne peut le voir. Il n'existe que pour couper editor.cpp,
// devenu trop long des que l'inspecteur a cesse de montrer un seul composant.

namespace editor {

// Dessine les panneaux de l'inspecteur pour une entite : ses composants, leurs reglages,
// et de quoi en ajouter ou en retirer.
void drawComponentPanels(scene::Scene& scene, const scene::ResourceTable& resources,
                         scene::Entity entity);

} // namespace editor
