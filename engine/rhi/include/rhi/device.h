#pragma once

#include "core/types.h"

namespace rhi {

class Mesh;
class RenderTarget;
class ShaderProgram;
class Texture;

// Les trois images du G-buffer, telles que la passe d'eclairage veut les lire.
enum class GBufferSlot : core::u32 {
    Albedo,
    Normal,
    Depth,
};

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

    // Dirige le dessin vers une cible intermediaire, puis de nouveau vers l'ecran.
    // Le viewport suit la taille de la cible : elle n'a aucune raison d'etre celle de la
    // fenetre, et ce sera le cas des shadow maps.
    void bindRenderTarget(const RenderTarget& target);
    void bindScreen(core::u32 width, core::u32 height);

    // Branche une texture sur une unite numerotee, celle que le shader lira.
    void bindTexture(const Texture& texture, core::u32 unit);
    void bindGBufferTexture(const RenderTarget& target, GBufferSlot slot, core::u32 unit);

    // Dessine un unique triangle couvrant l'ecran, sans aucune donnee de sommets : le
    // shader calcule les trois positions a partir du numero du sommet. C'est la passe qui
    // relit le G-buffer.
    void drawFullscreenTriangle(const ShaderProgram& program);

    // Dessine la geometrie avec ce programme. C'est le "draw call" : le seul ordre qui
    // declenche reellement du travail sur le GPU.
    void draw(const ShaderProgram& program, const Mesh& mesh);

private:
    bool m_created = false;
    // Le profil core exige un VAO lie pour tout dessin, meme sans attributs : celui-ci
    // reste vide et ne sert qu'au triangle plein ecran.
    core::u32 m_emptyVertexArray = 0;
};

} // namespace rhi
