#pragma once

#include "core/types.h"

#include <string_view>

namespace rhi {

// Un vertex shader et un fragment shader compiles puis lies ensemble. C'est l'unite
// que le GPU active pour dessiner.
class ShaderProgram {
public:
    ShaderProgram() = default;

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    // Compile les deux etages et les lie. En cas d'echec, le journal du pilote est
    // recopie dans nos logs : c'est la seule facon de savoir ce que GLSL reproche.
    bool create(std::string_view vertexSource, std::string_view fragmentSource);

    // Meme regle que Mesh::destroy : a appeler tant que le contexte GPU est vivant.
    void destroy();

private:
    friend class Device;

    core::u32 m_program = 0;
};

} // namespace rhi
