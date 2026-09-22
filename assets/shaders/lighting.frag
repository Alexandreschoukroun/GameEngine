#version 460 core

// Passe d'eclairage : elle relit le G-buffer et calcule, pour chaque pixel de l'ecran,
// la lumiere qui repart vers l'oeil. Son cout ne depend pas du nombre d'objets de la
// scene, seulement du nombre de pixels et de lumieres.

layout(binding = 0) uniform sampler2D uAlbedoRoughness;
layout(binding = 1) uniform sampler2D uNormalMetallic;
layout(binding = 2) uniform sampler2D uDepth;
// sampler2DShadow et non sampler2D : une lecture ne rend pas la profondeur stockee mais
// le resultat du test "ce point est-il devant ?", deja moyenne par le materiel.
layout(binding = 3) uniform sampler2DShadow uShadowMap;
// Cookie : la texture que la lumiere projette, qui salit son faisceau. Une vraie lampe
// n'emet pas un disque parfait - le reflecteur a des defauts, l'ampoule est decentree.
layout(binding = 4) uniform sampler2D uSpotCookie;

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
layout(location = 23) uniform vec4 uLightDirections[kMaxLights]; // xyz = direction, w = cos interieur
layout(location = 31) uniform vec4 uLightParams[kMaxLights];     // x = cos exterieur, y = type
layout(location = 39) uniform mat4 uShadowViewProjection;        // occupe 39 a 42
layout(location = 43) uniform int uShadowLightIndex;             // -1 = aucune ombre
layout(location = 44) uniform int uHasCookie;
// Environnement : deux couleurs, celle qui vient d'en haut et celle qui vient d'en bas.
// C'est le modele le plus simple qui ait un sens dans un interieur - la vraie carte
// d'environnement viendra si le besoin s'en fait sentir.
layout(location = 45) uniform vec4 uEnvironmentSky;    // rgb = couleur, a = intensite
layout(location = 46) uniform vec4 uEnvironmentGround; // rgb = couleur
// Exposition, en facteur multiplicatif deja calcule depuis les diaphragmes.
//
// Elle s'applique AVANT la courbe de rendu, et c'est tout son interet : la courbe ACES
// releve volontairement les valeurs basses pour preserver le detail dans les ombres, si
// bien qu'un mur eclaire par rien finit a 15 % de gris - de la penombre lisible, pas du
// noir. Diviser apres la courbe ne ferait que delaver l'image ; diviser avant deplace ce
// que la courbe considere comme sombre.
layout(location = 47) uniform float uExposure;

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
// Couleur que l'environnement renvoie dans une direction donnee. Un hemisphere : clair
// au-dessus, sombre en dessous, interpole entre les deux.
//
// Dans un interieur clos, cet "environnement" n'est pas un ciel : c'est la lumiere que
// les murs, le sol et le plafond se renvoient entre eux. On ne la simule pas, on
// l'approche par deux couleurs - ce qui suffit a ce qu'un metal ait quelque chose a
// reflechir, et c'est tout ce qui manquait.
vec3 environmentColor(vec3 direction) {
    float height = direction.y * 0.5 + 0.5;
    return mix(uEnvironmentGround.rgb, uEnvironmentSky.rgb, height) * uEnvironmentSky.a;
}

// Approximation analytique de l'integrale de la BRDF sur l'hemisphere, due a Lazarov.
// La methode exacte demande une texture precalculee ; cette courbe la remplace a quelques
// pour cent pres, pour le prix de six operations. Elle rend deux nombres : le facteur qui
// multiplie la reflectance de base, et celui qui s'y ajoute.
vec2 environmentBRDF(float nDotV, float roughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// Fresnel qui tient compte de la rugosite. Le Fresnel ordinaire suppose une surface
// parfaitement lisse ; sur une surface rugueuse, le reflet rasant est moins marque.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness) {
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 tonemapACES(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

// Le cookie se projette avec la meme matrice que l'ombre : la lumiere regarde sa texture
// exactement comme elle regarde sa carte de profondeur.
vec3 cookieTint(vec3 position) {
    vec4 lightSpace = uShadowViewProjection * vec4(position, 1.0);
    if (lightSpace.w <= 0.0) {
        return vec3(1.0); // derriere la lampe
    }
    vec2 uv = (lightSpace.xy / lightSpace.w) * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return vec3(0.0); // hors du cadre projete
    }
    return texture(uSpotCookie, uv).rgb;
}

// Proportion de lumiere qui atteint ce point : 1 en pleine lumiere, 0 dans l'ombre.
float shadowFactor(vec3 position, float nDotL) {
    vec4 lightSpace = uShadowViewProjection * vec4(position, 1.0);
    // Division perspective, puis passage de [-1,1] a [0,1] pour lire la texture.
    vec3 projected = lightSpace.xyz / lightSpace.w;
    projected = projected * 0.5 + 0.5;

    // Derriere la lumiere, ou au-dela de sa portee : la carte ne sait rien, on n'ombre pas.
    if (projected.z > 1.0) {
        return 1.0;
    }

    // Biais pentu : un texel de la carte couvre plusieurs centimetres, et l'erreur grandit
    // quand la surface est inclinee par rapport a la lumiere. Sans ce decalage, la surface
    // se fait de l'ombre a elle-meme et se couvre de rayures (acne d'ombre). Trop grand,
    // l'ombre se detache de l'objet, qui parait flotter (peter-panning).
    float bias = max(0.0015 * (1.0 - nDotL), 0.0004);

    // PCF : neuf comparaisons voisines, chacune deja adoucie par le filtrage materiel.
    // Sans ca, le bord de l'ombre suivrait les pixels de la carte, en escalier.
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float visibility = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * texelSize;
            visibility += texture(uShadowMap,
                                  vec3(projected.xy + offset, projected.z - bias));
        }
    }
    return visibility / 9.0;
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

        // Spot : la lumiere s'eteint progressivement entre le cone interieur et le cone
        // exterieur. Sans ce degrade, le bord serait une decoupe nette et artificielle.
        if (uLightParams[i].y > 0.5) {
            float cosAngle = dot(normalize(-light), normalize(uLightDirections[i].xyz));
            float cosInner = uLightDirections[i].w;
            float cosOuter = uLightParams[i].x;
            attenuation *= smoothstep(cosOuter, cosInner, cosAngle);
        }

        vec3 radiance = uLightColors[i].rgb * uLightColors[i].a * attenuation;

        float nDotL = max(dot(normal, light), 0.0);

        // Ombre portee : la seule information dont ce shader ne dispose pas localement.
        // Elle vient de la carte de profondeur rendue depuis la lumiere.
        if (i == uShadowLightIndex) {
            radiance *= shadowFactor(position, nDotL);
            if (uHasCookie != 0) {
                radiance *= cookieTint(position);
            }
        }
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

    // --- eclairage d'environnement ----------------------------------------------------
    //
    // Ce qui precede ne tient compte que des lampes. Or une surface recoit aussi la
    // lumiere que tout ce qui l'entoure lui renvoie - et pour un METAL, c'est la seule
    // source qui existe : un metal n'a aucune composante diffuse, il ne fait que
    // reflechir. Sans ce terme, tout metal est noir, quelles que soient les lampes.
    float nDotV = max(dot(normal, view), 0.0);
    vec3 fresnelAmbient = fresnelSchlickRoughness(nDotV, f0, roughness);

    // Diffus : la lumiere qui arrive de tout l'hemisphere au-dessus de la surface. Elle
    // est nulle sur un metal, et attenuee par ce qui part deja en reflet.
    vec3 ambientDiffuse = environmentColor(normal) * albedo * (1.0 - metallic) *
                          (vec3(1.0) - fresnelAmbient);

    // Speculaire : ce que la surface renvoie vers l'oeil. Une surface rugueuse "voit" une
    // zone large de l'environnement ; faute de pouvoir la flouter, on fait tendre la
    // direction de reflexion vers la normale, ce qui revient a moyenner.
    vec3 reflection = reflect(-view, normal);
    vec3 incoming = environmentColor(mix(reflection, normal, roughness));
    vec2 brdf = environmentBRDF(nDotV, roughness);
    vec3 ambientSpecular = incoming * (fresnelAmbient * brdf.x + brdf.y);

    color += ambientDiffuse + ambientSpecular;

    color *= uExposure;
    color = tonemapACES(color);
    // Derniere etape : reencoder vers la courbe sRGB attendue par l'ecran. Les calculs
    // precedents se font tous en lineaire.
    outColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
