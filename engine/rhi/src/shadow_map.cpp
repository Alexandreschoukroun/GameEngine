#include "rhi/shadow_map.h"

#include "core/log.h"

#include <glad/glad.h>

namespace rhi {

bool ShadowMap::create(core::u32 resolution) {
    if (resolution == 0) {
        core::logError("ShadowMap::create appele avec une resolution nulle");
        return false;
    }

    destroy();

    glCreateTextures(GL_TEXTURE_2D, 1, &m_depthTexture);
    glTextureStorage2D(m_depthTexture, 1, GL_DEPTH_COMPONENT24,
                       static_cast<GLsizei>(resolution), static_cast<GLsizei>(resolution));

    // Mode comparaison : une lecture ne rend plus la profondeur stockee mais le resultat
    // du test de profondeur, entre 0 et 1. Avec le filtrage lineaire, le materiel moyenne
    // quatre comparaisons voisines sans surcout : c'est un adoucissement gratuit.
    glTextureParameteri(m_depthTexture, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_depthTexture, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTextureParameteri(m_depthTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_depthTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Hors du cone de la lumiere, la carte ne contient aucune information. On borde donc
    // la texture d'une profondeur maximale, qui signifie "rien ne t'obstrue" : sans ca,
    // tout ce qui deborde serait dans l'ombre.
    const GLfloat borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTextureParameteri(m_depthTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_depthTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTextureParameterfv(m_depthTexture, GL_TEXTURE_BORDER_COLOR, borderColor);

    glCreateFramebuffers(1, &m_framebuffer);
    glNamedFramebufferTexture(m_framebuffer, GL_DEPTH_ATTACHMENT, m_depthTexture, 0);

    // Aucune sortie couleur : la passe d'ombre ne remplit que la profondeur, ce qui la
    // rend tres rapide. Sans ces deux lignes, le framebuffer serait juge incomplet.
    glNamedFramebufferDrawBuffer(m_framebuffer, GL_NONE);
    glNamedFramebufferReadBuffer(m_framebuffer, GL_NONE);

    if (glCheckNamedFramebufferStatus(m_framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        core::logError("framebuffer d'ombre incomplet");
        destroy();
        return false;
    }

    m_resolution = resolution;
    return true;
}

void ShadowMap::destroy() {
    if (m_framebuffer != 0) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }
    if (m_depthTexture != 0) {
        glDeleteTextures(1, &m_depthTexture);
        m_depthTexture = 0;
    }
    m_resolution = 0;
}

} // namespace rhi
