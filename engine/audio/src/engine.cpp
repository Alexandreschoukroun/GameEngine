#include "audio/engine.h"

#include "core/log.h"

// miniaudio est une bibliotheque a en-tete unique : sa definition est compilee ici, et
// nulle part ailleurs. C'est le meme mecanisme que stb_image dans la couche assets.
#define MINIAUDIO_IMPLEMENTATION
// On n'a besoin ni d'encoder, ni de lire du MP3 ou du FLAC : le WAV suffit pour nos
// effets, et retirer le reste allege la compilation autant que le binaire.
#define MA_NO_ENCODING
#define MA_NO_MP3
#define MA_NO_FLAC

// Le projet compile en /W4 /WX : le moindre avertissement est une erreur. Cette exigence
// vaut pour NOTRE code, pas pour les 90 000 lignes de C d'une bibliotheque tierce. On
// baisse donc le niveau le temps de l'inclure, et on le retablit juste apres.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <string>
#include <vector>

namespace audio {
namespace {

// Un identifiant de voix porte l'emplacement ET une generation. Sans la generation, un
// identifiant garde apres la fin d'un son designerait le son suivant qui occupe la meme
// place - on arreterait une autre source sans jamais comprendre pourquoi.
constexpr core::u32 kIndexBits = 16;
constexpr core::u32 kIndexMask = (1u << kIndexBits) - 1u;

VoiceHandle packVoice(core::u32 index, core::u32 generation) {
    return (generation << kIndexBits) | index;
}

core::u32 voiceIndex(VoiceHandle handle) { return handle & kIndexMask; }
core::u32 voiceGeneration(VoiceHandle handle) { return handle >> kIndexBits; }

} // namespace

struct Engine::Impl {
    ma_context context{};
    ma_engine engine{};
    bool contextReady = false;
    bool engineReady = false;

    // Un prototype par son charge : le fichier decode en memoire. Les voix en sont des
    // copies, qui partagent ses donnees sans les redecoder.
    struct Prototype {
        std::string path;
        ma_sound sound{};
    };
    std::vector<Prototype*> prototypes;

    struct Voice {
        ma_sound sound{};
        bool active = false;
        bool looping = false;
        core::u32 generation = 1;
    };
    std::vector<Voice> voices;
};

Engine::Engine() = default;

Engine::~Engine() { destroy(); }

bool Engine::create(bool silent) {
    if (m_impl != nullptr) {
        return true;
    }
    auto impl = std::make_unique<Impl>();

    ma_context_config contextConfig = ma_context_config_init();
    ma_result result = MA_SUCCESS;
    if (silent) {
        // Backend "null" : miniaudio simule une carte son au bon rythme, sans en ouvrir
        // aucune. C'est ce qui rend l'audio testable en CI, sur une machine muette.
        ma_backend backends[] = {ma_backend_null};
        result = ma_context_init(backends, 1, &contextConfig, &impl->context);
    } else {
        result = ma_context_init(nullptr, 0, &contextConfig, &impl->context);
    }
    if (result != MA_SUCCESS) {
        core::logError("audio : impossible d'initialiser le contexte");
        return false;
    }
    impl->contextReady = true;

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.pContext = &impl->context;
    if (ma_engine_init(&engineConfig, &impl->engine) != MA_SUCCESS) {
        core::logError("audio : impossible d'ouvrir le peripherique");
        ma_context_uninit(&impl->context);
        return false;
    }
    impl->engineReady = true;
    impl->voices.resize(kMaxVoices);

    m_impl = std::move(impl);
    core::logInfo(silent ? "moteur audio cree (silencieux)" : "moteur audio cree");
    return true;
}

void Engine::destroy() {
    if (m_impl == nullptr) {
        return;
    }
    // L'ordre compte : les voix referencent les prototypes, qui referencent le moteur.
    for (auto& voice : m_impl->voices) {
        if (voice.active) {
            ma_sound_uninit(&voice.sound);
            voice.active = false;
        }
    }
    for (Impl::Prototype* prototype : m_impl->prototypes) {
        ma_sound_uninit(&prototype->sound);
        delete prototype;
    }
    m_impl->prototypes.clear();
    if (m_impl->engineReady) {
        ma_engine_uninit(&m_impl->engine);
    }
    if (m_impl->contextReady) {
        ma_context_uninit(&m_impl->context);
    }
    m_impl.reset();
}

SoundHandle Engine::loadSound(std::string_view path) {
    if (m_impl == nullptr) {
        return kInvalidSound;
    }
    const std::string full(path);
    for (core::u32 i = 0; i < m_impl->prototypes.size(); ++i) {
        if (m_impl->prototypes[i]->path == full) {
            return i;
        }
    }

    auto* prototype = new Impl::Prototype();
    prototype->path = full;
    // DECODE : le fichier est entierement decode maintenant, en memoire. NO_SPATIALIZATION
    // sur le prototype seulement - il ne joue jamais, il sert de source aux copies.
    const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&m_impl->engine, full.c_str(), flags, nullptr, nullptr,
                                &prototype->sound) != MA_SUCCESS) {
        core::logWarn("audio : son introuvable ou illisible");
        core::logWarn(full);
        delete prototype;
        return kInvalidSound;
    }

    m_impl->prototypes.push_back(prototype);
    return static_cast<SoundHandle>(m_impl->prototypes.size() - 1);
}

VoiceHandle Engine::play(SoundHandle sound, const core::Vec3& position, bool looping,
                         core::f32 volume) {
    if (m_impl == nullptr || sound >= m_impl->prototypes.size()) {
        return kInvalidVoice;
    }

    core::u32 slot = kMaxVoices;
    for (core::u32 i = 0; i < m_impl->voices.size(); ++i) {
        if (!m_impl->voices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == kMaxVoices) {
        // Budget atteint. On refuse plutot que de voler la voix d'un autre son : couper
        // un pas ou un grincement en cours s'entendrait plus que l'absence du nouveau.
        core::logWarn("audio : budget de voix atteint");
        return kInvalidVoice;
    }

    Impl::Voice& voice = m_impl->voices[slot];
    // Copie du prototype : aucune relecture du fichier, les echantillons sont partages.
    if (ma_sound_init_copy(&m_impl->engine, &m_impl->prototypes[sound]->sound, 0, nullptr,
                           &voice.sound) != MA_SUCCESS) {
        core::logError("audio : impossible de demarrer une voix");
        return kInvalidVoice;
    }

    ma_sound_set_position(&voice.sound, position.x, position.y, position.z);
    ma_sound_set_looping(&voice.sound, looping ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&voice.sound, volume);
    // Distance minimale : en deca, le son ne monte plus. Sans ce plancher, l'attenuation
    // en 1/d ferait exploser le volume quand la source touche l'oreille.
    ma_sound_set_min_distance(&voice.sound, 1.0f);
    if (ma_sound_start(&voice.sound) != MA_SUCCESS) {
        ma_sound_uninit(&voice.sound);
        core::logError("audio : demarrage refuse");
        return kInvalidVoice;
    }

    voice.active = true;
    voice.looping = looping;
    return packVoice(slot, voice.generation);
}

void Engine::setVoicePosition(VoiceHandle voice, const core::Vec3& position) {
    if (!isVoicePlaying(voice)) {
        return;
    }
    Impl::Voice& slot = m_impl->voices[voiceIndex(voice)];
    ma_sound_set_position(&slot.sound, position.x, position.y, position.z);
}

bool Engine::isVoicePlaying(VoiceHandle voice) const {
    if (m_impl == nullptr || voice == kInvalidVoice) {
        return false;
    }
    const core::u32 index = voiceIndex(voice);
    if (index >= m_impl->voices.size()) {
        return false;
    }
    const Impl::Voice& slot = m_impl->voices[index];
    return slot.active && slot.generation == voiceGeneration(voice);
}

void Engine::stop(VoiceHandle voice) {
    if (!isVoicePlaying(voice)) {
        return;
    }
    Impl::Voice& slot = m_impl->voices[voiceIndex(voice)];
    ma_sound_uninit(&slot.sound);
    slot.active = false;
    // La generation avance : tous les identifiants deja distribues pour cet emplacement
    // deviennent invalides, et ne pourront plus agir sur le son qui prendra sa place.
    ++slot.generation;
}

void Engine::setListener(const core::Vec3& position, const core::Vec3& forward,
                         const core::Vec3& up) {
    if (m_impl == nullptr) {
        return;
    }
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
}

void Engine::update() {
    if (m_impl == nullptr) {
        return;
    }
    for (auto& voice : m_impl->voices) {
        if (voice.active && !voice.looping && ma_sound_at_end(&voice.sound) == MA_TRUE) {
            ma_sound_uninit(&voice.sound);
            voice.active = false;
            ++voice.generation;
        }
    }
}

core::u32 Engine::activeVoiceCount() const {
    if (m_impl == nullptr) {
        return 0;
    }
    core::u32 count = 0;
    for (const auto& voice : m_impl->voices) {
        if (voice.active) {
            ++count;
        }
    }
    return count;
}

void Engine::setMasterVolume(core::f32 volume) {
    if (m_impl != nullptr) {
        ma_engine_set_volume(&m_impl->engine, volume);
    }
}

} // namespace audio
