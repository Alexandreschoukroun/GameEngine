#pragma once

#include "core/types.h"

namespace rhi {

// Format de sommet du moteur : une position, et les coordonnees de texture qui lui
// correspondent. On ne generalise pas le format tant qu'il n'en existe qu'un seul
// reellement utilise (regle 5 du SPEC) ; les normales viendront avec l'eclairage en M2.
struct Vertex {
    core::f32 position[3];
    core::f32 uv[2];
};

// Geometrie prete a etre dessinee : les sommets copies dans la memoire de la carte,
// plus la description qui dit comment les relire.
class Mesh {
public:
    Mesh() = default;

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Les sommets sont stockes une fois, les indices decrivent les triangles qui les
    // reutilisent. Un cube tient ainsi en 24 sommets et 36 indices au lieu de 36 sommets.
    bool create(const Vertex* vertices, core::u32 vertexCount, const core::u32* indices,
                core::u32 indexCount);

    // A appeler tant que le contexte GPU est vivant. Il n'y a pas de destructeur qui le
    // fasse a notre place : un objet OpenGL detruit apres son contexte est un comportement
    // indefini, et l'ordre de destruction serait trop facile a casser par accident.
    void destroy();

    core::u32 indexCount() const { return m_indexCount; }

private:
    friend class Device;

    // Identifiants OpenGL (GLuint). Stockes en u32 pour que cet en-tete n'inclue aucun
    // en-tete OpenGL : la regle "pas de type GL au-dessus de rhi" vaut aussi ici.
    core::u32 m_vertexBuffer = 0;
    core::u32 m_indexBuffer = 0;
    core::u32 m_vertexArray = 0;
    core::u32 m_indexCount = 0;
};

} // namespace rhi
