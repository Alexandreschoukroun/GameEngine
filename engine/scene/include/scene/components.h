#pragma once

#include "core/math.h"
#include "core/types.h"
#include "renderer/light.h"

#include <string>

namespace rhi {
class Mesh;
class Texture;
} // namespace rhi

namespace scene {

// Les composants sont des DONNEES, sans comportement : pas de methode virtuelle, pas de
// logique. Les traitements vivent dans les systemes, qui parcourent les entites possedant
// les composants qui les interessent.

// Position, orientation et echelle dans le monde.
//
// C'est le SEUL endroit ou vit la position d'une entite. Une lumiere n'a pas de position
// propre : elle a un Transform. Sinon deux verites coexistent, et un jour elles divergent.
struct Transform {
    core::Vec3 position{0.0f, 0.0f, 0.0f};
    // Angles d'Euler en radians (tangage, lacet, roulis). Un quaternion serait plus robuste
    // pour des rotations composees ; on y viendra si le besoin apparait, pas avant.
    core::Vec3 rotation{0.0f, 0.0f, 0.0f};
    core::Vec3 scale{1.0f, 1.0f, 1.0f};

    core::Mat4 matrix() const;
    // Transposee de l'inverse : une normale ne se transforme pas comme un point. Avec une
    // echelle non uniforme, la matrice du modele la ferait sortir de la perpendiculaire.
    core::Mat3 normalMatrix() const;
};

// Ce qu'il faut pour dessiner l'entite. Les ressources sont designees par pointeur : la
// scene ne les possede pas, elle s'y refere.
struct MeshRenderer {
    const rhi::Mesh* mesh = nullptr;
    const rhi::Texture* baseColor = nullptr;
    const rhi::Texture* metallicRoughness = nullptr;
};

// Emission lumineuse. Ni position ni direction ici : elles viennent du Transform.
struct LightSource {
    core::Vec3 color{1.0f, 1.0f, 1.0f};
    core::f32 intensity = 10.0f;
    renderer::LightType type = renderer::LightType::Point;
    core::f32 innerAngleRadians = core::radians(16.0f);
    core::f32 outerAngleRadians = core::radians(26.0f);
    core::f32 range = 20.0f;
    bool castsShadow = false;
};

// Nom lisible, pour les logs aujourd'hui et l'inspecteur de l'editeur a M6.
struct Name {
    std::string value;
};

} // namespace scene
