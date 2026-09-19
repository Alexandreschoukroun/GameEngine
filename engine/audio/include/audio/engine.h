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
    //
    // La hauteur (pitch) multiplie la vitesse de lecture : 1 = telle quelle, 1,1 = un peu
    // plus aigu. Elle sert a varier un son repete - jouer cinquante fois le meme pas
    // exactement pareil trahit immediatement la machine. C'est moins couteux que de
    // stocker cinquante variantes du fichier.
    VoiceHandle play(SoundHandle sound, const core::Vec3& position, bool looping = false,
                     core::f32 volume = 1.0f, core::f32 pitch = 1.0f);

    // Demarre une voix NON SPATIALISEE : elle sort des deux enceintes a l'identique, sans
    // position ni attenuation. C'est ce qu'il faut pour une musique ou une ambiance de
    // fond - leur donner un endroit dans le monde n'aurait pas de sens, et le joueur ne
    // doit pas pouvoir s'en eloigner.
    VoiceHandle playAmbient(SoundHandle sound, bool looping = true, core::f32 volume = 1.0f);

    // Volume d'une voix deja lancee. L'occlusion s'applique par-dessus sans l'ecraser.
    void setVoiceVolume(VoiceHandle voice, core::f32 volume);
    core::f32 voiceVolume(VoiceHandle voice) const;

    // Deplace une voix deja lancee : une source attachee a un objet qui bouge.
    void setVoicePosition(VoiceHandle voice, const core::Vec3& position);

    // Position actuelle d'une voix. Symetrique de setVoicePosition : l'editeur de M6
    // devra dessiner les sources dans le viewport, et un test doit pouvoir verifier
    // qu'une voix suit bien l'entite qui la porte.
    core::Vec3 voicePosition(VoiceHandle voice) const;

    // Faux si la voix est terminee, arretee, ou si l'identifiant est perime.
    bool isVoicePlaying(VoiceHandle voice) const;

    void stop(VoiceHandle voice);

    // L'oreille : position et orientation. Chez nous, la camera. Sans cet appel, tout
    // sonnerait comme si le joueur regardait toujours vers -Z depuis l'origine.
    void setListener(const core::Vec3& position, const core::Vec3& forward,
                     const core::Vec3& up);

    // Occlusion d'une voix : 0 = rien entre la source et l'oreille, 1 = totalement
    // masquee. L'appelant calcule cette valeur (un mur ? une porte entrouverte ?) ; la
    // couche audio se contente de la traduire en son.
    //
    // Deux effets, parce qu'un seul ne suffirait pas a tromper l'oreille :
    //   - une ATTENUATION, parce qu'un obstacle absorbe de l'energie ;
    //   - un FILTRE PASSE-BAS, parce qu'un mur laisse passer les graves bien mieux que
    //     les aigus. C'est ce second effet qui rend un son "etouffe" plutot que
    //     simplement "moins fort", et c'est lui qui fait reconnaitre une porte fermee.
    void setVoiceOcclusion(VoiceHandle voice, core::f32 amount);

    // Valeur reellement appliquee, qui rejoint la consigne progressivement. Un rayon qui
    // clignote entre deux frames produirait sinon un cliquetis audible.
    core::f32 voiceOcclusion(VoiceHandle voice) const;

    // A appeler une fois par frame : libere les emplacements des voix terminees, et fait
    // avancer l'occlusion vers sa consigne. Sans ca, le budget se remplirait de sons deja
    // finis et l'occlusion sauterait d'un etat a l'autre.
    void update(core::f32 deltaSeconds);

    core::u32 activeVoiceCount() const;
    void setMasterVolume(core::f32 volume);

private:
    // Chemin commun aux deux lectures : seule la spatialisation les distingue.
    VoiceHandle startVoice(SoundHandle sound, const core::Vec3& position, bool looping,
                           core::f32 volume, core::f32 pitch, bool spatialized);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace audio
