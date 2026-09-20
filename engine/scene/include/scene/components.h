#pragma once

#include "core/math.h"
#include "core/types.h"
#include "core/uuid.h"
#include "renderer/light.h"
#include "scene/resource_table.h"

#include <entt/entity/entity.hpp>

#include <string>

namespace scene {

// Identifiant stable, conserve dans les fichiers de scene. L'identifiant EnTT, lui, est
// reattribue a chaque execution : il ne peut pas servir de reference durable.
struct Id {
    core::Uuid value = core::kInvalidUuid;
};

// Les composants sont des DONNEES, sans comportement : pas de methode virtuelle, pas de
// logique. Les traitements vivent dans les systemes, qui parcourent les entites possedant
// les composants qui les interessent.

// Position, orientation et echelle dans le monde.
//
// C'est le SEUL endroit ou vit la position d'une entite. Une lumiere n'a pas de position
// propre : elle a un Transform. Sinon deux verites coexistent, et un jour elles divergent.
struct Transform {
    core::Vec3 position{0.0f, 0.0f, 0.0f};
    // Quaternion, et non plus des angles d'Euler : la physique en produit, et c'est la
    // seule representation qui compose proprement et s'interpole sans blocage de cardan.
    // L'editeur de M6 affichera des degres tout en stockant ceci, comme les moteurs du
    // commerce.
    core::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    core::Vec3 scale{1.0f, 1.0f, 1.0f};

    core::Mat4 matrix() const;
    // Transposee de l'inverse : une normale ne se transforme pas comme un point. Avec une
    // echelle non uniforme, la matrice du modele la ferait sortir de la perpendiculaire.
    core::Mat3 normalMatrix() const;
};

// Lien vers le parent. L'enfant est alors place RELATIVEMENT a lui : bouger le parent
// deplace toute sa descendance. Une poignee sur une porte, une lampe sur une table.
struct Parent {
    entt::entity value = entt::null;
};

// Matrice monde, calculee une fois par frame a partir du Transform local et de la chaine
// des parents. C'est elle que consomment le rendu, et plus tard la physique et l'audio :
// sans ca, chacun referait le meme calcul de son cote.
//
// epoch sert de memo pendant la passe : une entite deja calculee cette frame n'est pas
// recalculee, meme si plusieurs enfants la reclament comme parent.
struct WorldTransform {
    core::Mat4 matrix{1.0f};
    core::u32 epoch = 0;
};

// Ce qu'il faut pour dessiner l'entite. Les ressources sont designees par pointeur : la
// scene ne les possede pas, elle s'y refere.
// Des poignees et non des pointeurs : une adresse memoire change a chaque lancement, donc
// ne peut pas etre ecrite dans un fichier. La ResourceTable fait la correspondance.
struct MeshRenderer {
    ResourceHandle mesh = kInvalidResource;
    // UNE matiere, et non trois textures. C'est plus court dans le fichier, plus juste
    // ("du bois", pas "cette couleur avec ce relief"), et c'est l'unite que les modeles
    // glTF apportent avec eux comme celle que l'editeur manipulera.
    ResourceHandle material = kInvalidResource;
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
