#pragma once

#include "core/types.h"

#include <vector>

namespace assets {

// Image decodee en RGBA 8 bits, ligne du haut en premier.
struct ImageData {
    core::u32 width = 0;
    core::u32 height = 0;
    std::vector<core::u8> pixels;

    bool isValid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<std::size_t>(width) * height * 4;
    }
};

// Decode un fichier image (PNG, JPEG, TGA...) vers du RGBA 8 bits.
bool loadImage(const char* path, ImageData& out);

} // namespace assets
