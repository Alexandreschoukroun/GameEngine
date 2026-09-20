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
// Carte de normales, elle aussi lineaire. Une surface plane y vaut (0.5, 0.5, 1) - le
// fameux bleu lavande des cartes de normales, qui signifie "aucune deviation".
layout(binding = 2) uniform sampler2D uNormalMap;

// Vaut 0 quand le maillage n'a pas de tangentes, ou n'a pas de carte de normales : on
// garde alors la normale geometrique telle quelle.
layout(location = 12) uniform float uNormalMapStrength;

// Facteurs du materiau glTF. Ils MULTIPLIENT les textures, et valent 1 quand le fichier
// n'en dit rien - un materiau sans carte de couleur mais avec un facteur rouge decrit donc
// un objet rouge uni, ce qui est le cas de beaucoup de modeles simples.
layout(location = 13) uniform vec4 uBaseColorFactor;
layout(location = 14) uniform float uMetallicFactor;
layout(location = 15) uniform float uRoughnessFactor;

in vec3 vNormal;
in vec4 vTangent;
in vec2 vTexCoord;

void main() {
    vec3 baseColor = texture(uAlbedo, vTexCoord).rgb * uBaseColorFactor.rgb;
    vec2 metallicRoughness = texture(uMetallicRoughness, vTexCoord).bg;
    metallicRoughness.x *= uMetallicFactor;
    metallicRoughness.y *= uRoughnessFactor;

    // L'interpolation entre trois sommets raccourcit les normales : il faut renormaliser.
    // En 16 bits flottants on stocke les valeurs negatives telles quelles, sans l'encodage
    // * 0.5 + 0.5 qu'imposerait un format 8 bits.
    vec3 normal = normalize(vNormal);

    if (uNormalMapStrength > 0.0) {
        // Re-orthogonalisation de Gram-Schmidt : l'interpolation entre sommets a pu
        // rapprocher la tangente de la normale, et un repere non perpendiculaire
        // deformerait le relief.
        vec3 tangent = normalize(vTangent.xyz - normal * dot(normal, vTangent.xyz));
        // La bitangente se DEDUIT des deux autres : la stocker couterait douze octets par
        // sommet pour une information qu'un produit vectoriel redonne. Le signe w decide
        // de son sens, et c'est lui qui rend un relief correct sur une texture miroitee.
        vec3 bitangent = cross(normal, tangent) * vTangent.w;

        // La carte stocke ses valeurs dans [0,1] : on les ramene dans [-1,1].
        vec3 sampled = texture(uNormalMap, vTexCoord).xyz * 2.0 - 1.0;
        // Le repere tangent transforme une normale exprimee "par rapport a la surface" en
        // une normale du monde. C'est tout l'interet d'une carte de normales : la meme
        // texture fonctionne sur n'importe quelle surface, quelle que soit son orientation.
        mat3 tangentToWorld = mat3(tangent, bitangent, normal);
        vec3 mapped = normalize(tangentToWorld * sampled);
        normal = normalize(mix(normal, mapped, uNormalMapStrength));
    }

    outAlbedoRoughness = vec4(baseColor, metallicRoughness.y);
    outNormalMetallic = vec4(normal, metallicRoughness.x);
}
