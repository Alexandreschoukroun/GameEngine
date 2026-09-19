#version 460 core

// Passe de geometrie : elle n'eclaire rien, elle decrit les surfaces visibles.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 0) uniform mat4 uViewProjection;

out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    // Les positions et les normales arrivent deja en coordonnees du monde : le chargeur
    // glTF a applique la transformation des noeuds. Il n'y a donc pas encore de matrice
    // modele ici ; elle apparaitra quand la scene saura deplacer ses objets (M3).
    vNormal = aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
