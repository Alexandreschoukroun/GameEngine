#version 460 core

// Passe de geometrie : elle n'eclaire rien, elle decrit les surfaces visibles.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aTexCoord;

// Une mat4 occupe quatre emplacements consecutifs : la camera prend 0 a 3, le modele
// 4 a 7, et la matrice des normales 8 a 10.
layout(location = 0) uniform mat4 uViewProjection;
layout(location = 4) uniform mat4 uModel;
layout(location = 8) uniform mat3 uNormalMatrix;

out vec3 vNormal;
out vec4 vTangent;
out vec2 vTexCoord;

void main() {
    // Du repere du modele vers le monde, puis du monde vers l'espace clip.
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vNormal = uNormalMatrix * aNormal;
    // La tangente est couchee DANS la surface : elle se transforme comme une direction
    // ordinaire, pas comme une normale. Le signe w, lui, ne se transforme pas.
    vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * worldPosition;
}
