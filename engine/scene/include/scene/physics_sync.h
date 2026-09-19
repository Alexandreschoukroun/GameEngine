#pragma once

#include "core/math.h"
#include "core/types.h"
#include "physics/world.h"
#include "scene/scene.h"

namespace scene {

// Forme de collision. Une boite suffit aujourd'hui : murs, sols, caisses, portes. La
// capsule arrivera avec le controleur de personnage, la forme issue du maillage avec le
// decor detaille.
enum class ColliderShape : core::u32 {
    Box = 0,
};

// Description de la collision d'une entite, telle qu'elle est ecrite dans le fichier de
// scene. Ce composant est de la DONNEE : il ne contient aucun identifiant de corps, qui
// n'existe qu'a l'execution.
struct Collider {
    ColliderShape shape = ColliderShape::Box;
    core::Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    // Statique : ne bouge jamais, ne subit pas la gravite. C'est le cas de tout le decor,
    // et c'est ce qui le rend presque gratuit a simuler.
    bool isStatic = true;
    // Masse volumique en kg/m3, dont decoulent la masse et l'inertie. 1000 est la valeur
    // par defaut de Jolt, celle d'un solide plein ; une porte en bois creux est a 150.
    // Ignoree pour un corps statique, dont la masse est infinie par definition.
    core::f32 density = 1000.0f;
};

// Charniere : la porte pivote autour d'un axe, entre deux butees. L'ancrage est donne
// RELATIVEMENT au centre de l'entite - typiquement un demi-battant sur le cote, la ou se
// trouveraient les gonds.
struct Hinge {
    core::Vec3 localAnchor{0.0f, 0.0f, 0.0f};
    core::Vec3 axis{0.0f, 1.0f, 0.0f};
    core::f32 minAngle = -1.6f; // environ -92 degres
    core::f32 maxAngle = 1.6f;
    // Couple de frottement des gonds, en N.m. A doser selon l'inertie du battant : pour
    // une porte de 20 kg (inertie ~5 kg.m2), 6 N.m l'arretent en une seconde environ.
    // Multiplier sa masse par sept imposerait de multiplier ce couple d'autant. Sans
    // friction du tout, elle tournerait comme un battant de saloon.
    core::f32 friction = 6.0f;
};

// Corps physique associe, cree a l'execution. Volontairement separe du Collider : il n'a
// aucun sens dans un fichier, puisqu'un identifiant de corps change a chaque lancement.
struct PhysicsBody {
    physics::BodyHandle handle = physics::kInvalidBody;
};

// Cree un corps pour chaque entite qui a un Collider et n'en a pas encore.
//
// Appelable a tout moment : une entite ajoutee en cours de partie recevra son corps au
// prochain appel, sans traitement particulier.
void createPhysicsBodies(Scene& scene, physics::World& world);

// Recopie la pose des corps dynamiques dans les Transform.
//
// Le sens compte : la simulation fait autorite sur la position d'un objet dynamique.
// Ecrire dans un Transform gere par la physique donnerait un objet qui tremble, tiraille
// entre deux verites.
void syncTransformsFromPhysics(Scene& scene, const physics::World& world);

} // namespace scene
