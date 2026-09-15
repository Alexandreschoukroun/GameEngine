#pragma once

#include "core/log.h"

#include <cstdlib>
#include <sstream>

#if defined(_MSC_VER)
#define ENGINE_DEBUG_BREAK() __debugbreak()
#else
#define ENGINE_DEBUG_BREAK() __builtin_trap()
#endif

#if defined(NDEBUG)
#define ENGINE_ASSERT(condition, message) ((void)0)
#else
#define ENGINE_ASSERT(condition, message)                                             \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::ostringstream engineAssertStream;                                    \
            engineAssertStream << "assertion failed: (" #condition ") at " << __FILE__ \
                                << ":" << __LINE__ << " - " << message;               \
            ::core::logError(engineAssertStream.str());                               \
            ENGINE_DEBUG_BREAK();                                                     \
            std::abort();                                                             \
        }                                                                             \
    } while (0)
#endif
