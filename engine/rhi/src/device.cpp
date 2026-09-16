#include "rhi/device.h"

#include "core/assert.h"
#include "core/log.h"
#include "rhi/mesh.h"
#include "rhi/shader_program.h"
#include "rhi/texture.h"

#include <glad/glad.h>

#include <cstdio>
#include <string_view>

namespace rhi {
namespace {

const char* debugTypeLabel(GLenum type) {
    switch (type) {
        case GL_DEBUG_TYPE_ERROR: return "erreur";
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "obsolete";
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "indefini";
        case GL_DEBUG_TYPE_PORTABILITY: return "portabilite";
        case GL_DEBUG_TYPE_PERFORMANCE: return "performance";
        default: return "divers";
    }
}

// Appelee par le driver a chaque erreur ou avertissement. Elle ne doit jamais lever
// d'exception ni allouer : on se contente de recopier le message dans nos logs.
void APIENTRY onDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity,
                             GLsizei length, const GLchar* message, const void* userParam) {
    (void)source;
    (void)id;
    (void)userParam;

    // Le message n'est pas garanti termine par zero : on utilise la longueur donnee.
    const std::string_view text(message, static_cast<std::size_t>(length));

    char buffer[512];
    const int written = std::snprintf(buffer, sizeof(buffer), "GL[%s] %.*s", debugTypeLabel(type),
                                      static_cast<int>(text.size()), text.data());
    if (written <= 0) {
        return;
    }

    const std::string_view line(buffer, static_cast<std::size_t>(written) < sizeof(buffer)
                                            ? static_cast<std::size_t>(written)
                                            : sizeof(buffer) - 1);
    if (severity == GL_DEBUG_SEVERITY_HIGH) {
        core::logError(line);
    } else {
        core::logWarn(line);
    }
}

void logDriverIdentity() {
    const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "OpenGL %s | %s | %s", version ? version : "?",
                  renderer ? renderer : "?", vendor ? vendor : "?");
    core::logInfo(buffer);
}

void enableDebugOutput() {
    if (!GLAD_GL_VERSION_4_3) {
        core::logWarn("debug output OpenGL indisponible (contexte < 4.3)");
        return;
    }

    glEnable(GL_DEBUG_OUTPUT);
    // Synchrone : le driver appelle notre fonction au moment exact de l'erreur, donc la pile
    // d'appels du debogueur pointe la ligne fautive. Ca coute un peu de performance, on ne
    // l'active donc qu'en Debug.
#if !defined(NDEBUG)
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif
    glDebugMessageCallback(onDebugMessage, nullptr);

    // Les pilotes emettent beaucoup de messages purement informatifs : on les coupe.
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr,
                          GL_FALSE);
}

} // namespace

bool Device::create(ProcAddressLoader loader) {
    if (loader == nullptr) {
        core::logError("chargeur de fonctions OpenGL absent");
        return false;
    }

    if (gladLoadGLLoader(loader) == 0) {
        core::logError("chargement des fonctions OpenGL echoue");
        return false;
    }

    if (!GLAD_GL_VERSION_4_6) {
        core::logError("OpenGL 4.6 indisponible sur ce pilote");
        return false;
    }

    logDriverIdentity();
    enableDebugOutput();

    m_created = true;
    return true;
}

void Device::setViewport(core::u32 width, core::u32 height) {
    ENGINE_ASSERT(m_created, "Device::create doit reussir avant tout appel GPU");
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void Device::clear(core::f32 red, core::f32 green, core::f32 blue, core::f32 alpha) {
    ENGINE_ASSERT(m_created, "Device::create doit reussir avant tout appel GPU");
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Device::bindTexture(const Texture& texture, core::u32 unit) {
    ENGINE_ASSERT(m_created, "Device::create doit reussir avant tout appel GPU");
    glBindTextureUnit(static_cast<GLuint>(unit), texture.m_texture);
}

void Device::draw(const ShaderProgram& program, const Mesh& mesh) {
    ENGINE_ASSERT(m_created, "Device::create doit reussir avant tout appel GPU");
    if (program.m_program == 0 || mesh.m_vertexArray == 0) {
        return;
    }

    glUseProgram(program.m_program);
    glBindVertexArray(mesh.m_vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.m_vertexCount));
}

} // namespace rhi
