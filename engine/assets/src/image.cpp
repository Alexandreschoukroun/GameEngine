#include "assets/image.h"

#include "core/log.h"

// stb_image est une bibliotheque d'un seul fichier : sa definition est generee ici, dans
// une unique unite de compilation. Les avertissements de ce code tiers sont coupes : il
// n'est pas soumis a nos regles de style, et /WX les transformerait en erreurs.
#define STB_IMAGE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace assets {

bool loadImage(const char* path, ImageData& out) {
    int width = 0;
    int height = 0;
    int channelsInFile = 0;

    // Le dernier argument force la sortie en RGBA, quel que soit le format d'entree :
    // le moteur n'a ainsi qu'une seule disposition de pixels a gerer.
    stbi_uc* pixels = stbi_load(path, &width, &height, &channelsInFile, 4);
    if (pixels == nullptr) {
        core::logError(stbi_failure_reason() != nullptr ? stbi_failure_reason()
                                                        : "image illisible");
        core::logError(path);
        return false;
    }

    out.width = static_cast<core::u32>(width);
    out.height = static_cast<core::u32>(height);
    const std::size_t byteCount = static_cast<std::size_t>(width) * height * 4;
    out.pixels.assign(pixels, pixels + byteCount);

    stbi_image_free(pixels);
    return out.isValid();
}

} // namespace assets
