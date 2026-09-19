#version 460 core

// Ce shader n'ecrit pas une image mais DEUX, en une seule passe : c'est le MRT.
// Chaque sortie correspond a un attachement de la cible de rendu.
//
// Les canaux alpha ne sont pas perdus : ils transportent les deux parametres de matiere.
// Un troisieme attachement aurait coute 4 octets par pixel, soit 8 Mo en 1080p, pour la
// meme information.
//
//   attachement 0 : RGB = couleur de base (sRGB)   A = rugosite   (lineaire)
//   attachement 1 : RGB = normale du monde (16F)   A = metallicite (lineaire)
layout(location = 0) out vec4 outAlbedoRoughness;
layout(location = 1) out vec4 outNormalMetallic;

layout(binding = 0) uniform sampler2D uAlbedo;
// Convention glTF : le canal vert porte la rugosite, le bleu la metallicite. Cette texture
// est branchee en format lineaire : ce sont des mesures, pas des couleurs a regarder.
layout(binding = 1) uniform sampler2D uMetallicRoughness;

in vec3 vNormal;
in vec2 vTexCoord;

void main() {
    vec3 baseColor = texture(uAlbedo, vTexCoord).rgb;
    vec2 metallicRoughness = texture(uMetallicRoughness, vTexCoord).bg;

    // L'interpolation entre trois sommets raccourcit les normales : il faut renormaliser.
    // En 16 bits flottants on stocke les valeurs negatives telles quelles, sans l'encodage
    // * 0.5 + 0.5 qu'imposerait un format 8 bits.
    vec3 normal = normalize(vNormal);

    outAlbedoRoughness = vec4(baseColor, metallicRoughness.y);
    outNormalMetallic = vec4(normal, metallicRoughness.x);
}
