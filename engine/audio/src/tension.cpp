#include "audio/tension.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>

namespace audio {
namespace {

// Vitesse a laquelle la tension rejoint sa consigne. Volontairement LENTE : la tension
// doit monter comme une inquietude, pas comme un interrupteur. Une seconde et demie pour
// l'essentiel du trajet.
constexpr core::f32 kTensionRate = 1.6f;

// Volume maximal de chaque couche. Elles se superposent, donc aucune ne monte a 1 : trois
// couches a plein volume saturent le melange.
constexpr core::f32 kCalmGain = 0.55f;
constexpr core::f32 kPulseGain = 0.45f;
constexpr core::f32 kEdgeGain = 0.40f;

// Interpolation douce entre deux seuils : 0 avant, 1 apres, et une courbe en S entre les
// deux. Une rampe lineaire ferait entendre le debut et la fin de chaque entree de couche.
core::f32 smoothstep(core::f32 edge0, core::f32 edge1, core::f32 value) {
    if (edge1 <= edge0) {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const core::f32 t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

core::f32 TensionLayer::layerVolume(Layer layer, core::f32 tension) {
    const core::f32 t = std::clamp(tension, 0.0f, 1.0f);
    switch (layer) {
        case Layer::Calm:
            // Jamais absente, mais elle s'efface a moitie quand la tension monte : c'est
            // ce qui laisse la place aux autres sans creer de trou.
            return kCalmGain * (1.0f - 0.5f * t);
        case Layer::Pulse:
            // Entre tot et occupe tout le milieu de la course.
            return kPulseGain * smoothstep(0.15f, 0.65f, t);
        case Layer::Edge:
            // N'apparait que dans le dernier tiers. Si la dissonance etait la des le
            // debut, il ne resterait rien a escalader.
            return kEdgeGain * smoothstep(0.60f, 1.0f, t);
        case Layer::Count:
        default:
            return 0.0f;
    }
}

bool TensionLayer::start(Engine& engine, const std::array<SoundHandle, 3>& sounds) {
    stop(engine);

    for (core::u32 i = 0; i < sounds.size(); ++i) {
        if (sounds[i] == kInvalidSound) {
            core::logWarn("tension : couche manquante, musique desactivee");
            stop(engine);
            return false;
        }
        // Non spatialisee : la musique n'a pas d'endroit, et le joueur ne doit pas
        // pouvoir s'en eloigner.
        m_voices[i] = engine.playAmbient(sounds[i], true, 0.0f);
        if (m_voices[i] == kInvalidVoice) {
            stop(engine);
            return false;
        }
    }

    // Les trois couches demarrent ensemble et ne s'arretent jamais : c'est ce qui garantit
    // qu'elles restent en phase. Les faire entrer et sortir produirait des raccords
    // audibles a chaque variation de tension.
    m_applied = false;
    update(engine, 0.0f);
    return true;
}

void TensionLayer::stop(Engine& engine) {
    for (VoiceHandle& voice : m_voices) {
        engine.stop(voice);
        voice = kInvalidVoice;
    }
    m_applied = false;
}

void TensionLayer::setTension(core::f32 tension) {
    m_target = std::clamp(tension, 0.0f, 1.0f);
}

void TensionLayer::update(Engine& engine, core::f32 deltaSeconds) {
    if (!m_applied) {
        // Premiere application : on prend la consigne telle quelle, sinon chaque niveau
        // commencerait par une montee de tension que personne n'a demandee.
        m_tension = m_target;
        m_applied = true;
    } else {
        const core::f32 blend = 1.0f - std::exp(-kTensionRate * deltaSeconds);
        m_tension += (m_target - m_tension) * blend;
    }

    for (core::u32 i = 0; i < m_voices.size(); ++i) {
        if (m_voices[i] == kInvalidVoice) {
            continue;
        }
        engine.setVoiceVolume(m_voices[i], layerVolume(static_cast<Layer>(i), m_tension));
    }
}

} // namespace audio
