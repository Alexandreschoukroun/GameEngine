#version 460 core

// Passe d'ombre : on redessine la scene depuis la lumiere, en n'enregistrant que la
// profondeur. Ni normale, ni UV, ni texture : seule la position compte.

layout(location = 0) in vec3 aPosition;
// Les attributs 1 et 2 existent dans le maillage mais ne sont pas declares ici : un shader
// n'est pas oblige de lire tous les attributs du VAO.

layout(location = 0) uniform mat4 uLightViewProjection;

void main() {
    gl_Position = uLightViewProjection * vec4(aPosition, 1.0);
}
