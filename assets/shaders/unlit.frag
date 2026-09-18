#version 460 core

// "unlit" : la couleur de base est affichee telle quelle, sans aucun eclairage. C'est
// l'etat du renderer avant les etapes 3 et 4 de M2.

// binding = 0 : l'unite de texture sur laquelle le moteur branche l'image.
layout(binding = 0) uniform sampler2D uAlbedo;

in vec2 vTexCoord;
out vec4 outColor;

void main() {
    outColor = texture(uAlbedo, vTexCoord);
}
