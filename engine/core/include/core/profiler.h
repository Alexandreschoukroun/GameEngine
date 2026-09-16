#pragma once

// Instrumentation pour Tracy.
//
// Le moteur n'appelle jamais Tracy directement : il passe par ces deux macros. Le jour ou
// on changerait de profileur, ou si l'option GAMEENGINE_PROFILING est desactivee (builds
// distribues), il n'y a qu'ici a toucher et le code instrumente ne change pas.
//
// Une macro ne peut pas etre "cachee" dans un .cpp : elle s'expanse chez l'appelant, donc
// l'en-tete de Tracy doit etre visible. C'est la deuxieme exception assumee a la regle 2
// du SPEC, apres GLM, et pour la meme raison : core est la couche la plus basse.
#if defined(ENGINE_PROFILING_ENABLED)
#include <tracy/Tracy.hpp>

// Marque la fin d'une image. C'est ce qui permet a Tracy de raisonner par frame
// (histogramme, temps par image) plutot qu'en temps absolu.
#define ENGINE_PROFILE_FRAME() FrameMark

// Mesure le bloc courant, du point de declaration jusqu'a la fin de la portee.
#define ENGINE_PROFILE_SCOPE(name) ZoneScopedN(name)
#else
#define ENGINE_PROFILE_FRAME() ((void)0)
#define ENGINE_PROFILE_SCOPE(name) ((void)0)
#endif
