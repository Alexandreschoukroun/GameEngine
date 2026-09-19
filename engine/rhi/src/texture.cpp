#include "rhi/texture.h"

#include "core/log.h"

#include <glad/glad.h>

namespace rhi {
namespace {

// Nombre de mipmaps jusqu'au niveau 1x1 : 256x256 en donne 9 (256, 128, ... 1).
core::u32 mipLevelCount(core::u32 width, core::u32 height) {
    core::u32 size = width > height ? width : height;
    core::u32 levels = 1;
    while (size > 1) {
        size /= 2;
        ++levels;
    }
    return levels;
}

} // namespace

bool Texture::create(core::u32 width, core::u32 height, const core::u8* pixelsRgba8,
                     TextureFormat format) {
    if (width == 0 || height == 0 || pixelsRgba8 == nullptr) {
        core::logError("Texture::create appele sans pixels");
        return false;
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_texture);

    // Storage reserve toute la chaine de mipmaps d'un coup, avec un format definitif.
    //
    // SRGB8_ALPHA8 : le GPU convertit les octets en valeurs lineaires a chaque lecture,
    // gratuitement, dans le materiel. Sans ca, les calculs d'eclairage porteraient sur des
    // valeurs perceptuelles et seraient faux. Le canal alpha, lui, reste toujours lineaire.
    const GLenum internalFormat =
        format == TextureFormat::SrgbColor ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    const GLsizei levels = static_cast<GLsizei>(mipLevelCount(width, height));
    glTextureStorage2D(m_texture, levels, internalFormat, static_cast<GLsizei>(width),
                       static_cast<GLsizei>(height));

    // Remplit le niveau 0 avec nos pixels...
    glTextureSubImage2D(m_texture, 0, 0, 0, static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, pixelsRgba8);
    // ... puis laisse le pilote calculer les niveaux suivants.
    glGenerateTextureMipmap(m_texture);

    // Agrandissement : moyenne des 4 texels voisins. Reduction : moyenne entre les deux
    // mipmaps encadrant la distance (filtrage trilineaire), sinon l'image scintille au loin.
    glTextureParameteri(m_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    // Hors de l'intervalle 0..1, l'image se repete.
    glTextureParameteri(m_texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_texture, GL_TEXTURE_WRAP_T, GL_REPEAT);

    return true;
}

void Texture::destroy() {
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
}

} // namespace rhi
