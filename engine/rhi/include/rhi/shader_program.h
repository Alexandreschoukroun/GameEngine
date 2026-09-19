#pragma once

#include "core/math.h"
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

    // Envoie une matrice au programme. L'emplacement est celui declare cote GLSL par
    // layout(location = N), ce qui evite d'avoir a le chercher par son nom au demarrage.
    void setMat4(core::u32 location, const core::Mat4& value);
    void setInt(core::u32 location, core::i32 value);
    void setVec2(core::u32 location, const core::Vec2& value);

    // Meme regle que Mesh::destroy : a appeler tant que le contexte GPU est vivant.
    void destroy();

private:
    friend class Device;

    core::u32 m_program = 0;
};

} // namespace rhi
