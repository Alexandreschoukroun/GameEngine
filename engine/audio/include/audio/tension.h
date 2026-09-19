#pragma once

#include "audio/engine.h"
#include "core/types.h"

#include <array>

namespace audio {

// Couche de tension : la musique du jeu, pilotee par une seule variable de 0 a 1.
//
// Le systeme ne joue PAS des morceaux. Trois couches tournent en permanence, superposees,
// et seuls leurs volumes varient. C'est ce qu'on appelle un remixage vertical, et c'est ce
// que le SPEC demande explicitement - "un drone parametrique pilote par une variable
// tension, pas des morceaux fixes".
//
// La difference se joue sur la REACTIVITE. Un morceau fixe doit etre attendu, enchaine,
// ou coupe brutalement ; des couches superposees suivent la situation en continu, et le
// joueur ne percoit aucune transition. C'est indispensable pour un AI Director (M7) qui
// module la tension seconde par seconde.
class TensionLayer {
public:
    // Les trois couches, du calme a l'agression.
    enum class Layer : core::u32 {
        Calm = 0,  // un grave permanent : le lit sur lequel les autres se posent
        Pulse = 1, // un battement lent : quelque chose se prepare
        Edge = 2,  // une dissonance aigue : la confrontation
        Count = 3,
    };

    // Demarre les trois couches a volume nul, puis applique la tension courante. Rend faux
    // si un son manque - le jeu continue alors sans musique.
    bool start(Engine& engine, const std::array<SoundHandle, 3>& sounds);
    void stop(Engine& engine);

    // Consigne, bornee a [0, 1]. La valeur appliquee la rejoint progressivement : une
    // tension qui saute s'entend comme une erreur, pas comme une intention.
    void setTension(core::f32 tension);
    core::f32 tension() const { return m_tension; }
    core::f32 target() const { return m_target; }

    // A appeler une fois par frame.
    void update(Engine& engine, core::f32 deltaSeconds);

    // Volume theorique d'une couche pour une tension donnee. Expose parce que c'est la
    // regle de melange elle-meme, et qu'elle merite d'etre testee sans moteur audio.
    static core::f32 layerVolume(Layer layer, core::f32 tension);

private:
    std::array<VoiceHandle, 3> m_voices{kInvalidVoice, kInvalidVoice, kInvalidVoice};
    core::f32 m_target = 0.0f;
    core::f32 m_tension = 0.0f;
    bool m_applied = false;
};

} // namespace audio
