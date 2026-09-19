#include "rhi/shader_program.h"

#include "core/log.h"

#include <glad/glad.h>

namespace rhi {
namespace {

void logInfoLog(GLuint object, bool isProgram, std::string_view prefix) {
    GLint length = 0;
    if (isProgram) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    }
    if (length <= 0) {
        core::logError(prefix);
        return;
    }

    // Le journal du pilote peut etre long : on le tronque plutot que d'allouer.
    char buffer[1024];
    const GLsizei capacity = static_cast<GLsizei>(sizeof(buffer));
    GLsizei written = 0;
    if (isProgram) {
        glGetProgramInfoLog(object, capacity, &written, buffer);
    } else {
        glGetShaderInfoLog(object, capacity, &written, buffer);
    }

    core::logError(prefix);
    core::logError(std::string_view(buffer, static_cast<std::size_t>(written)));
}

// Compile un etage. Rend 0 en cas d'echec.
GLuint compileStage(GLenum stage, std::string_view source, std::string_view label) {
    const GLuint shader = glCreateShader(stage);

    // On passe la longueur explicitement : la source n'est pas forcement terminee par un
    // zero, puisqu'elle vient d'un string_view.
    const GLchar* text = source.data();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        logInfoLog(shader, false, label);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

bool ShaderProgram::create(std::string_view vertexSource, std::string_view fragmentSource) {
    const GLuint vertexShader = compileStage(GL_VERTEX_SHADER, vertexSource,
                                             "compilation du vertex shader echouee");
    if (vertexShader == 0) {
        return false;
    }

    const GLuint fragmentShader = compileStage(GL_FRAGMENT_SHADER, fragmentSource,
                                               "compilation du fragment shader echouee");
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return false;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    // Les etages compiles ne servent plus une fois lies : le programme en garde une copie.
    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        logInfoLog(program, true, "edition de liens du programme de shaders echouee");
        glDeleteProgram(program);
        return false;
    }

    m_program = program;
    return true;
}

void ShaderProgram::setMat4(core::u32 location, const core::Mat4& value) {
    if (m_program == 0) {
        return;
    }
    // glProgramUniform... (4.1+) designe le programme au lieu d'exiger qu'il soit actif :
    // meme logique DSA que partout ailleurs. GLM range ses matrices par colonnes, comme
    // OpenGL les attend, d'ou transpose = GL_FALSE.
    glProgramUniformMatrix4fv(m_program, static_cast<GLint>(location), 1, GL_FALSE,
                              &value[0][0]);
}

void ShaderProgram::setInt(core::u32 location, core::i32 value) {
    if (m_program == 0) {
        return;
    }
    glProgramUniform1i(m_program, static_cast<GLint>(location), value);
}

void ShaderProgram::setVec2(core::u32 location, const core::Vec2& value) {
    if (m_program == 0) {
        return;
    }
    glProgramUniform2f(m_program, static_cast<GLint>(location), value.x, value.y);
}

void ShaderProgram::setVec3(core::u32 location, const core::Vec3& value) {
    if (m_program == 0) {
        return;
    }
    glProgramUniform3f(m_program, static_cast<GLint>(location), value.x, value.y, value.z);
}

void ShaderProgram::setVec4(core::u32 location, const core::Vec4& value) {
    if (m_program == 0) {
        return;
    }
    glProgramUniform4f(m_program, static_cast<GLint>(location), value.x, value.y, value.z,
                       value.w);
}

void ShaderProgram::setVec4Array(core::u32 location, const core::Vec4* values,
                                 core::u32 count) {
    if (m_program == 0 || values == nullptr || count == 0) {
        return;
    }
    glProgramUniform4fv(m_program, static_cast<GLint>(location), static_cast<GLsizei>(count),
                        &values[0][0]);
}

void ShaderProgram::destroy() {
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

} // namespace rhi
