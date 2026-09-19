#include "rhi/render_target.h"

#include "core/log.h"

#include <glad/glad.h>

namespace rhi {
namespace {

// Textures de cible de rendu : un seul niveau (pas de mipmaps), filtrage simple, et
// surtout CLAMP_TO_EDGE. Repeter une cible de rendu n'aurait aucun sens et ferait
// reapparaitre le bord oppose de l'ecran en cas de lecture legerement hors cadre.
core::u32 createTargetTexture(GLenum internalFormat, core::u32 width, core::u32 height) {
    GLuint texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    glTextureStorage2D(texture, 1, internalFormat, static_cast<GLsizei>(width),
                       static_cast<GLsizei>(height));
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

} // namespace

bool RenderTarget::create(core::u32 width, core::u32 height) {
    if (width == 0 || height == 0) {
        core::logError("RenderTarget::create appele avec une taille nulle");
        return false;
    }

    destroy(); // recreation a chaque redimensionnement de la fenetre

    // RGBA8 pour la couleur : 8 bits par canal suffisent a une couleur de base.
    m_albedoTexture = createTargetTexture(GL_RGBA8, width, height);
    // RGBA16F pour la normale : en 8 bits, les surfaces courbes montreraient des bandes.
    m_normalTexture = createTargetTexture(GL_RGBA16F, width, height);
    // Profondeur en texture, et non en renderbuffer : la passe suivante doit la relire
    // pour reconstruire la position de chaque pixel.
    m_depthTexture = createTargetTexture(GL_DEPTH_COMPONENT24, width, height);

    glCreateFramebuffers(1, &m_framebuffer);
    glNamedFramebufferTexture(m_framebuffer, GL_COLOR_ATTACHMENT0, m_albedoTexture, 0);
    glNamedFramebufferTexture(m_framebuffer, GL_COLOR_ATTACHMENT1, m_normalTexture, 0);
    glNamedFramebufferTexture(m_framebuffer, GL_DEPTH_ATTACHMENT, m_depthTexture, 0);

    // Sans cette liste, seul l'attachement 0 recevrait les ecritures : c'est elle qui
    // active le MRT, donc l'ecriture simultanee dans plusieurs images.
    const GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glNamedFramebufferDrawBuffers(m_framebuffer, 2, drawBuffers);

    const GLenum status = glCheckNamedFramebufferStatus(m_framebuffer, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        core::logError("framebuffer incomplet");
        destroy();
        return false;
    }

    m_width = width;
    m_height = height;
    return true;
}

void RenderTarget::destroy() {
    if (m_framebuffer != 0) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }
    const GLuint textures[] = {m_albedoTexture, m_normalTexture, m_depthTexture};
    for (GLuint texture : textures) {
        if (texture != 0) {
            glDeleteTextures(1, &texture);
        }
    }
    m_albedoTexture = 0;
    m_normalTexture = 0;
    m_depthTexture = 0;
    m_width = 0;
    m_height = 0;
}

} // namespace rhi
