#version 460 core

// Ce shader n'ecrit pas une image mais DEUX, en une seule passe : c'est le MRT.
// Chaque sortie correspond a un attachement de la cible de rendu.
layout(location = 0) out vec4 outAlbedo; // attachement 0 : couleur de base, RGBA8
layout(location = 1) out vec4 outNormal; // attachement 1 : normale du monde, RGBA16F

layout(binding = 0) uniform sampler2D uAlbedo;

in vec3 vNormal;
in vec2 vTexCoord;

void main() {
    outAlbedo = texture(uAlbedo, vTexCoord);

    // L'interpolation entre trois sommets raccourcit les normales : il faut renormaliser.
    // En 16 bits flottants on peut stocker les valeurs negatives telles quelles, sans
    // l'encodage * 0.5 + 0.5 qu'imposerait un format 8 bits.
    outNormal = vec4(normalize(vNormal), 0.0);
}
