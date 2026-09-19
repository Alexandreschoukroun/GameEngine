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
