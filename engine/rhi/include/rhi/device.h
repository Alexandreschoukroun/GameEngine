#pragma once

#include "core/types.h"

namespace rhi {

// Signature d'une fonction qui rend l'adresse d'une fonction OpenGL a partir de son nom.
// C'est le seul lien entre rhi et la couche fenetrage : aucun type SDL ne remonte ici,
// aucun type OpenGL ne descend la-bas.
using ProcAddressLoader = void* (*)(const char* name);

// Point d'entree de la couche GPU. A ce stade elle ne sait que trois choses :
// charger OpenGL, effacer l'ecran et definir la zone de dessin.
class Device {
public:
    Device() = default;

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // Charge les fonctions OpenGL modernes et branche le rapport d'erreurs du driver.
    // A appeler une fois, apres la creation du contexte, depuis le thread qui le possede.
    bool create(ProcAddressLoader loader);

    void setViewport(core::u32 width, core::u32 height);
    void clear(core::f32 red, core::f32 green, core::f32 blue, core::f32 alpha);

private:
    bool m_created = false;
};

} // namespace rhi
