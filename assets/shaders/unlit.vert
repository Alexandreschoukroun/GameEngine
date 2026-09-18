#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

// location = 0 : l'emplacement d'uniforme que le moteur remplit avec la matrice de la
// camera. Il est declare ici, donc rien a chercher par son nom cote C++.
layout(location = 0) uniform mat4 uViewProjection;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    // Du monde vers l'espace clip.
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
