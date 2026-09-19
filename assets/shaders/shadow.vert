#version 460 core

// Passe d'ombre : on redessine la scene depuis la lumiere, en n'enregistrant que la
// profondeur. Ni normale, ni UV, ni texture : seule la position compte.

layout(location = 0) in vec3 aPosition;
// Les attributs 1 et 2 existent dans le maillage mais ne sont pas declares ici : un shader
// n'est pas oblige de lire tous les attributs du VAO.

layout(location = 0) uniform mat4 uLightViewProjection;
// Meme emplacement que dans gbuffer.vert, pour que le renderer n'ait pas deux cas.
layout(location = 4) uniform mat4 uModel;

void main() {
    gl_Position = uLightViewProjection * uModel * vec4(aPosition, 1.0);
}
