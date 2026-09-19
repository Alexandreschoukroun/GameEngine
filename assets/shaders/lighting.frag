#version 460 core

// Passe d'eclairage : elle relit le G-buffer et calcule, pour chaque pixel de l'ecran,
// la lumiere qui repart vers l'oeil. Son cout ne depend pas du nombre d'objets de la
// scene, seulement du nombre de pixels et de lumieres.

layout(binding = 0) uniform sampler2D uAlbedoRoughness;
layout(binding = 1) uniform sampler2D uNormalMetallic;
layout(binding = 2) uniform sampler2D uDepth;

const int kMaxLights = 8; // le budget du SPEC : ~8 lumieres visibles simultanement

layout(location = 0) uniform int uView;             // 0 = eclaire, 1..3 = debogage
layout(location = 1) uniform vec2 uNearFar;
layout(location = 2) uniform mat4 uInverseViewProjection;
layout(location = 3) uniform vec3 uCameraPosition;
layout(location = 6) uniform int uLightCount;
// Un tableau de 8 vec4 occupe 8 emplacements consecutifs : les positions prennent 7 a 14,
// d'ou les couleurs a partir de 15.
layout(location = 7) uniform vec4 uLightPositions[kMaxLights];  // xyz = position
layout(location = 15) uniform vec4 uLightColors[kMaxLights];    // rgb = couleur, a = puissance

in vec2 vTexCoord;
out vec4 outColor;

const float kPi = 3.14159265359;

// La position n'est pas stockee dans le G-buffer : on la retrouve depuis la profondeur et
// l'inverse de la matrice de camera. Trois canaux economises par pixel.
vec3 worldPositionFromDepth(float storedDepth) {
    // De l'espace ecran vers l'espace clip : les coordonnees vont de -1 a +1.
    vec4 clip = vec4(vTexCoord * 2.0 - 1.0, storedDepth * 2.0 - 1.0, 1.0);
    vec4 world = uInverseViewProjection * clip;
    // La division perspective, que le GPU fait automatiquement dans l'autre sens.
    return world.xyz / world.w;
}

float linearizeDepth(float storedDepth, float near, float far) {
    float ndc = storedDepth * 2.0 - 1.0;
    return (2.0 * near * far) / (far + near - ndc * (far - near));
}

// --- Les trois termes du modele a microfacettes -----------------------------------------

// D : quelle proportion des micro-miroirs est orientee pile pour renvoyer la lumiere vers
// l'oeil. C'est ce terme qui fait la tache brillante, large si la surface est rugueuse.
float distributionGGX(vec3 normal, vec3 halfway, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfway), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * denominator * denominator);
}

// G : les micro-miroirs se font de l'ombre entre eux, d'autant plus que la surface est
// rugueuse et qu'on la regarde de biais.
float geometrySchlick(float nDotX, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return nDotX / (nDotX * (1.0 - k) + k);
}

float geometrySmith(vec3 normal, vec3 view, vec3 light, float roughness) {
    return geometrySchlick(max(dot(normal, view), 0.0), roughness) *
           geometrySchlick(max(dot(normal, light), 0.0), roughness);
}

// F : tout materiau devient reflechissant quand on le regarde en rasant. C'est pour ca
// qu'un sol mat brille en fin de couloir.
vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// --- Reduction des valeurs d'eclairage a ce qu'un ecran sait afficher --------------------

// L'eclairage n'a pas de plafond : une lampe proche peut donner 50 la ou l'ecran affiche
// 1 au maximum. Couper a 1 cramerait tout en blanc plat ; cette approximation de la courbe
// ACES garde du detail dans les hautes lumieres.
vec3 tonemapACES(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main() {
    vec4 albedoRoughness = texture(uAlbedoRoughness, vTexCoord);
    vec4 normalMetallic = texture(uNormalMetallic, vTexCoord);
    float storedDepth = texture(uDepth, vTexCoord).r;

    // Vues de debogage : les donnees brutes, avant tout calcul.
    if (uView == 1) {
        outColor = vec4(albedoRoughness.rgb, 1.0);
        return;
    }
    if (uView == 2) {
        outColor = vec4(normalMetallic.xyz * 0.5 + 0.5, 1.0);
        return;
    }
    if (uView == 3) {
        float distance = linearizeDepth(storedDepth, uNearFar.x, uNearFar.y);
        outColor = vec4(vec3(distance / 10.0), 1.0);
        return;
    }

    // Profondeur a 1 : aucun objet n'a ete dessine ici, c'est le fond.
    if (storedDepth >= 1.0) {
        outColor = vec4(0.01, 0.0, 0.005, 1.0);
        return;
    }

    vec3 albedo = albedoRoughness.rgb;
    float roughness = clamp(albedoRoughness.a, 0.04, 1.0); // 0 pur donnerait une division par zero
    vec3 normal = normalize(normalMetallic.xyz);
    float metallic = normalMetallic.a;

    vec3 position = worldPositionFromDepth(storedDepth);
    vec3 view = normalize(uCameraPosition - position);

    // Les dielectriques reflechissent environ 4 % de la lumiere de face ; un metal
    // reflechit sa propre couleur. C'est la seule difference de fond entre les deux.
    vec3 f0 = mix(vec3(0.04), albedo, metallic);

    // Chaque lumiere ajoute sa contribution. Le cout de cette boucle est le vrai prix de
    // l'eclairage : il se paie une fois par pixel de l'ecran, quel que soit le nombre
    // d'objets de la scene. C'est tout l'interet du rendu differe.
    vec3 color = vec3(0.0);
    for (int i = 0; i < uLightCount && i < kMaxLights; ++i) {
        vec3 toLight = uLightPositions[i].xyz - position;
        float lightDistance = length(toLight);
        vec3 light = toLight / max(lightDistance, 0.0001);
        vec3 halfway = normalize(view + light);

        // La lumiere se dilue sur une sphere dont la surface croit comme le carre du
        // rayon : d'ou la decroissance en 1/d^2, realite physique et non reglage.
        float attenuation = 1.0 / (lightDistance * lightDistance);
        vec3 radiance = uLightColors[i].rgb * uLightColors[i].a * attenuation;

        float nDotL = max(dot(normal, light), 0.0);
        vec3 fresnel = fresnelSchlick(max(dot(halfway, view), 0.0), f0);
        float distribution = distributionGGX(normal, halfway, roughness);
        float geometry = geometrySmith(normal, view, light, roughness);

        vec3 specular = (distribution * geometry * fresnel) /
                        max(4.0 * max(dot(normal, view), 0.0) * nDotL, 0.0001);

        // Conservation de l'energie : ce qui part en reflet ne peut pas repartir en
        // diffus. Et un metal n'a pas de composante diffuse du tout.
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec3 diffuse = diffuseWeight * albedo / kPi;

        color += (diffuse + specular) * radiance * nDotL;
    }

    // Ambiante tres faible : dans un jeu d'horreur, l'obscurite doit rester noire, mais
    // pas au point qu'on ne devine plus rien.
    color += albedo * 0.015;

    color = tonemapACES(color);
    // Derniere etape : reencoder vers la courbe sRGB attendue par l'ecran. Les calculs
    // precedents se font tous en lineaire.
    outColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
