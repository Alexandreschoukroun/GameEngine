#pragma once

#include "core/types.h"

namespace rhi {

// Format de sommet du moteur. Il n'y en a qu'un aujourd'hui : une position dans l'espace.
// Il gagnera ses coordonnees de texture a l'etape suivante. On ne generalise pas avant
// d'avoir deux formats reellement utilises.
struct Vertex {
    core::f32 position[3];
};

// Geometrie prete a etre dessinee : les sommets copies dans la memoire de la carte,
// plus la description qui dit comment les relire.
class Mesh {
public:
    Mesh() = default;

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    bool create(const Vertex* vertices, core::u32 count);

    // A appeler tant que le contexte GPU est vivant. Il n'y a pas de destructeur qui le
    // fasse a notre place : un objet OpenGL detruit apres son contexte est un comportement
    // indefini, et l'ordre de destruction serait trop facile a casser par accident.
    void destroy();

    core::u32 vertexCount() const { return m_vertexCount; }

private:
    friend class Device;

    // Identifiants OpenGL (GLuint). Stockes en u32 pour que cet en-tete n'inclue aucun
    // en-tete OpenGL : la regle "pas de type GL au-dessus de rhi" vaut aussi ici.
    core::u32 m_vertexBuffer = 0;
    core::u32 m_vertexArray = 0;
    core::u32 m_vertexCount = 0;
};

} // namespace rhi
