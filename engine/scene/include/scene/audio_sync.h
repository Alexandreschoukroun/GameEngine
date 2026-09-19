#pragma once

#include "audio/engine.h"
#include "core/types.h"
#include "physics/world.h"
#include "scene/resource_table.h"
#include "scene/scene.h"

namespace scene {

// Source sonore attachee a une entite, telle qu'elle est ecrite dans le fichier de scene.
// C'est de la DONNEE : aucun identifiant de voix ici, qui n'existe qu'a l'execution.
//
// Le son sort de la position de l'entite. S'il s'agit d'un enfant, c'est sa position dans
// le MONDE qui compte : une braise accrochee a une statue qui tourne doit s'entendre
// tourner avec elle.
struct AudioSource {
    ResourceHandle sound = kInvalidResource;
    core::f32 volume = 1.0f;
    // Une ambiance tourne en boucle ; un impact ne se joue qu'une fois. Les sources de la
    // scene sont des ambiances par nature - un son unique declenche au chargement du
    // niveau ne s'entendrait jamais.
    bool looping = true;
};

// Voix en cours pour cette entite. Jamais serialise : un identifiant de voix change a
// chaque lancement, exactement comme un identifiant de corps physique.
struct AudioVoice {
    audio::VoiceHandle handle = audio::kInvalidVoice;
};

// Demarre les sources qui n'ont pas encore de voix. A appeler apres le chargement de la
// scene, une fois les sons enregistres dans la table de ressources.
void startAudioSources(Scene& scene, const ResourceTable& resources, audio::Engine& engine);

// Recopie la position monde de chaque entite sonore dans sa voix. A appeler chaque frame.
//
// Le sens du flux est l'INVERSE de celui de la physique : la simulation fait autorite sur
// la position d'un corps et la scene la recopie, tandis que l'audio ne decide de rien - il
// se contente de suivre la scene. Une source ne pouvant pas deplacer ce qu'elle sonorise,
// il n'y a ici aucune ambiguite sur qui detient la verite.
void syncAudioSources(Scene& scene, audio::Engine& engine);

// Calcule ce qui separe chaque source de l'oreille, et le transmet au moteur audio.
//
// Trois rayons plutot qu'un seul : un rayon unique donnerait une occlusion binaire, qui
// basculerait brutalement quand le joueur bouge d'un pas. Les deux rayons lateraux
// permettent une occlusion PARTIELLE - exactement ce qu'il faut pour une porte entrouverte,
// ou une partie du son passe et l'autre non.
void updateAudioOcclusion(Scene& scene, const physics::World& world, audio::Engine& engine,
                          const core::Vec3& listenerPosition);

} // namespace scene
