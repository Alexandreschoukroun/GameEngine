#version 460 core

// Passe d'affichage : elle relit le G-buffer et montre une de ses couches.
// L'eclairage prendra sa place a l'etape 4 ; en attendant, cette passe sert a verifier
// de ses propres yeux ce que la passe de geometrie a ecrit.

layout(binding = 0) uniform sampler2D uAlbedo;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform sampler2D uDepth;

layout(location = 0) uniform int uView;      // 0 = couleur, 1 = normale, 2 = profondeur
layout(location = 1) uniform vec2 uNearFar;  // plans de la camera, pour lineariser

in vec2 vTexCoord;
out vec4 outColor;

// La profondeur stockee n'est pas une distance : la precision est concentree pres de la
// camera, si bien qu'affichee telle quelle l'image parait uniformement blanche. Cette
// fonction retrouve la distance reelle en metres.
float linearizeDepth(float storedDepth, float near, float far) {
    float ndc = storedDepth * 2.0 - 1.0; // [0,1] -> [-1,1], convention OpenGL
    return (2.0 * near * far) / (far + near - ndc * (far - near));
}

void main() {
    if (uView == 1) {
        // Une normale va de -1 a +1 ; on la ramene dans [0,1] pour l'afficher.
        // Rouge = tournee vers +X, vert = vers +Y, bleu = vers +Z.
        vec3 normal = texture(uNormal, vTexCoord).xyz;
        outColor = vec4(normal * 0.5 + 0.5, 1.0);
    } else if (uView == 2) {
        float distance = linearizeDepth(texture(uDepth, vTexCoord).r, uNearFar.x, uNearFar.y);
        // Divise par 10 m : au-dela, tout est blanc. Sans cette echelle, le modele occupe
        // une fraction invisible de l'intervalle.
        outColor = vec4(vec3(distance / 10.0), 1.0);
    } else {
        outColor = texture(uAlbedo, vTexCoord);
    }
}
