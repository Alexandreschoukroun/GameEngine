#pragma once

#include "audio/engine.h"
#include "core/types.h"
#include "physics/world.h"
#include "scene/resource_table.h"
#include "scene/scene.h"

namespace scene {

// Matiere d'une surface, telle qu'elle est ecrite dans le fichier de scene. C'est de la
// DONNEE, comme le collider qu'elle accompagne.
//
// Aujourd'hui elle ne porte que le son des pas. Elle est appelee a porter davantage : le
// SPEC prevoit que les impacts et les trainees d'objets en dependent aussi, et l'occlusion
// devrait distinguer une porte en bois d'un mur de pierre. La masse volumique introduite
// en M4 pour la porte est la meme information, vue par la physique.
struct Surface {
    ResourceHandle footstep = kInvalidResource;
};

// Entite a laquelle appartient un corps physique, ou kInvalidEntity.
//
// Recherche lineaire : un niveau compte quelques centaines de corps, et cette fonction
// n'est appelee qu'au moment d'un pas. Le jour ou elle sera appelee par image et par
// corps, elle demandera une table - pas avant, le SPEC interdit d'optimiser a l'aveugle.
Entity entityForBody(const Scene& scene, physics::BodyHandle body);

// Joue les pas du joueur en fonction de ce qu'il foule.
//
// La cadence est reglee par la DISTANCE parcourue, pas par le temps : courir produit donc
// des pas plus rapproches sans aucun reglage supplementaire, et s'arreter les arrete.
class FootstepPlayer {
public:
    // Distance entre deux pas, en metres, et volume des pas.
    void configure(core::f32 strideLength, core::f32 volume);

    // Avance d'une frame. Rend la ressource jouee, ou kInvalidResource si aucun pas n'a
    // ete declenche - ce qui permet de tester la cadence sans interroger le moteur audio,
    // et servira a l'ouie de l'antagoniste en M7 : un pas est un bruit qu'il peut entendre.
    ResourceHandle update(const Scene& scene, const physics::World& world,
                          const ResourceTable& resources, audio::Engine& engine,
                          const core::Vec3& feetPosition, bool onGround,
                          core::f32 travelledDistance);

private:
    core::f32 m_strideLength = 0.75f;
    core::f32 m_volume = 0.5f;
    core::f32 m_accumulated = 0.0f;
    // Generateur simple et local : deux joueurs n'ont aucune raison de partager un etat,
    // et on ne veut pas dependre de l'aleatoire global du programme.
    core::u32 m_noise = 0x9e3779b9u;
};

} // namespace scene
