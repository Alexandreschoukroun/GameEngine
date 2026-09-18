#include "rhi/mesh.h"

#include "core/log.h"

#include <glad/glad.h>

#include <cstddef>

namespace rhi {

bool Mesh::create(const Vertex* vertices, core::u32 vertexCount, const core::u32* indices,
                  core::u32 indexCount) {
    if (vertices == nullptr || vertexCount == 0 || indices == nullptr || indexCount == 0) {
        core::logError("Mesh::create appele sans sommets ou sans indices");
        return false;
    }

    // glCreateBuffers (4.5+) cree l'objet sans l'attacher a quoi que ce soit : c'est le
    // style DSA. L'ancien glGenBuffers ne creait qu'un nom, l'objet n'existant qu'au
    // premier glBindBuffer.
    glCreateBuffers(1, &m_vertexBuffer);

    // Storage et non Data : la taille est definitive. Le pilote sait que ces octets ne
    // seront jamais reallouees et peut les placer en consequence.
    glNamedBufferStorage(m_vertexBuffer, static_cast<GLsizeiptr>(sizeof(Vertex) * vertexCount),
                         vertices, 0);

    // Meme principe pour les indices : un buffer de plus dans la carte graphique.
    glCreateBuffers(1, &m_indexBuffer);
    glNamedBufferStorage(m_indexBuffer,
                         static_cast<GLsizeiptr>(sizeof(core::u32) * indexCount), indices, 0);

    // Le VAO ne contient pas de donnees : il decrit comment relire celles du buffer.
    glCreateVertexArrays(1, &m_vertexArray);

    // Point de liaison 0 : on y branche notre buffer, avec le pas entre deux sommets.
    glVertexArrayVertexBuffer(m_vertexArray, 0, m_vertexBuffer, 0,
                              static_cast<GLsizei>(sizeof(Vertex)));

    // Attribut 0 : la position, 3 flottants a son offset dans Vertex.
    glEnableVertexArrayAttrib(m_vertexArray, 0);
    glVertexArrayAttribFormat(m_vertexArray, 0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(Vertex, position)));
    glVertexArrayAttribBinding(m_vertexArray, 0, 0);

    // Attribut 1 : les coordonnees de texture, 2 flottants. Les deux attributs partagent
    // le meme buffer et le meme pas : ils sont entrelaces dans chaque sommet.
    glEnableVertexArrayAttrib(m_vertexArray, 1);
    glVertexArrayAttribFormat(m_vertexArray, 1, 2, GL_FLOAT, GL_FALSE,
                              static_cast<GLuint>(offsetof(Vertex, uv)));
    glVertexArrayAttribBinding(m_vertexArray, 1, 0);

    // Le VAO retient aussi quel buffer d'indices utiliser : un seul objet a lier au moment
    // de dessiner, et non deux.
    glVertexArrayElementBuffer(m_vertexArray, m_indexBuffer);

    m_indexCount = indexCount;
    return true;
}

void Mesh::destroy() {
    if (m_vertexArray != 0) {
        glDeleteVertexArrays(1, &m_vertexArray);
        m_vertexArray = 0;
    }
    if (m_indexBuffer != 0) {
        glDeleteBuffers(1, &m_indexBuffer);
        m_indexBuffer = 0;
    }
    if (m_vertexBuffer != 0) {
        glDeleteBuffers(1, &m_vertexBuffer);
        m_vertexBuffer = 0;
    }
    m_indexCount = 0;
}

} // namespace rhi
