#version 460 core

// Passe de geometrie : elle n'eclaire rien, elle decrit les surfaces visibles.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

// Une mat4 occupe quatre emplacements consecutifs : la camera prend 0 a 3, le modele
// 4 a 7, et la matrice des normales 8 a 10.
layout(location = 0) uniform mat4 uViewProjection;
layout(location = 4) uniform mat4 uModel;
layout(location = 8) uniform mat3 uNormalMatrix;

out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    // Du repere du modele vers le monde, puis du monde vers l'espace clip.
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vNormal = uNormalMatrix * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * worldPosition;
}
