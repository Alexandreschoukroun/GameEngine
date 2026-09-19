#pragma once

#include "core/math.h"
#include "core/types.h"

#include <memory>
#include <string>
#include <string_view>

namespace audio {

// Un son charge, pret a etre joue autant de fois qu'on veut. C'est la donnee.
using SoundHandle = core::u32;
inline constexpr SoundHandle kInvalidSound = 0xFFFFFFFFu;

// Une instance en cours de lecture. C'est ce qui vit : deux voix peuvent jouer le meme
// son a deux endroits differents. La distinction est la meme qu'entre un maillage et
// une entite qui l'affiche.
using VoiceHandle = core::u32;
inline constexpr VoiceHandle kInvalidVoice = 0xFFFFFFFFu;

// Budget du SPEC. Au-dela, jouer un son de plus echoue proprement plutot que de faire
// grossir le melange jusqu'a saturer le processeur.
inline constexpr core::u32 kMaxVoices = 64;

// Moteur audio. Il possede le peripherique, le thread temps reel qui le nourrit, et les
// voix en cours.
//
// Aucun type de miniaudio n'apparait ici : la regle 2 du SPEC vaut pour l'audio comme
// pour Jolt. L'API ne parle que de Vec3, de chaines et d'identifiants entiers.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Ouvre la carte son. En mode silencieux, miniaudio utilise son backend "null" : tout
    // le moteur fonctionne, rien ne sort. C'est ce qui permet de tester l'audio en CI, ou
    // aucune carte son n'existe.
    bool create(bool silent = false);
    void destroy();

    // Charge et decode un fichier. Le chemin est complet : c'est l'appelant qui le resout
    // avec platform::assetPath, exactement comme pour les maillages et les images - la
    // couche audio n'a pas a savoir ou vivent les donnees.
    //
    // Le meme chemin demande deux fois rend le meme identifiant : le fichier n'est decode
    // qu'une fois. Le decodage a lieu ICI, jamais pendant la lecture : lire un fichier
    // depuis le thread temps reel provoquerait une coupure audible.
    //
    // Un son destine a etre spatialise doit etre MONO. Un fichier stereo est deja reparti
    // entre les enceintes par son auteur ; lui donner une position n'aurait pas de sens.
    SoundHandle loadSound(std::string_view path);

    // Demarre une voix a une position du monde. Rend kInvalidVoice si le budget est
    // atteint ou si le son est invalide.
    VoiceHandle play(SoundHandle sound, const core::Vec3& position, bool looping = false,
                     core::f32 volume = 1.0f);

    // Deplace une voix deja lancee : une source attachee a un objet qui bouge.
    void setVoicePosition(VoiceHandle voice, const core::Vec3& position);

    // Faux si la voix est terminee, arretee, ou si l'identifiant est perime.
    bool isVoicePlaying(VoiceHandle voice) const;

    void stop(VoiceHandle voice);

    // L'oreille : position et orientation. Chez nous, la camera. Sans cet appel, tout
    // sonnerait comme si le joueur regardait toujours vers -Z depuis l'origine.
    void setListener(const core::Vec3& position, const core::Vec3& forward,
                     const core::Vec3& up);

    // A appeler une fois par frame : libere les emplacements des voix terminees. Sans ca,
    // le budget se remplirait de sons deja finis.
    void update();

    core::u32 activeVoiceCount() const;
    void setMasterVolume(core::f32 volume);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace audio
