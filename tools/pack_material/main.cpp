// Empaquette une carte metallicite/rugosite au format glTF.
//
// Le probleme qu'il resout : les jeux de textures libres (ambientCG, Poly Haven) livrent
// la rugosite et la metallicite dans des fichiers SEPARES, en niveaux de gris. Le format
// glTF, lui, les veut dans UNE SEULE image, chaque grandeur dans son canal :
//
//   R = occlusion ambiante   G = rugosite   B = metallicite
//
// Cet outil fait la conversion. Il est ecrit en C++ et non en script pour deux raisons :
// il reutilise le chargeur d'images du moteur, donc il lit exactement ce que le moteur
// sait lire, et la CI le compile - il ne peut pas se decorreler du format attendu.
//
// Usage :
//   pack_material sortie.png --roughness <fichier|valeur> --metalness <fichier|valeur>
//                            [--ao <fichier>]
//
// Une VALEUR au lieu d'un fichier donne une constante, ce qui est le cas courant : la
// plupart des matieres non metalliques n'ont aucune carte de metallicite.

#include "assets/image.h"
#include "core/log.h"
#include "core/types.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <charconv>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Une entree : soit une image, soit une valeur constante de 0 a 255.
struct Channel {
    assets::ImageData image;
    core::u8 constant = 0;
    bool isImage = false;
};

void printUsage() {
    std::printf(
        "Usage : pack_material <sortie.png> --roughness <fichier|0..1>\n"
        "                      --metalness <fichier|0..1> [--ao <fichier>]\n\n"
        "Produit la carte metallicite/rugosite attendue par le moteur :\n"
        "  R = occlusion ambiante   G = rugosite   B = metallicite\n\n"
        "Attention aux cartes de NORMALES : prenez la variante OpenGL (NormalGL,\n"
        "nor_gl) et non DirectX, dont le canal vert est inverse.\n");
}

// Rend vrai si le texte est un nombre de 0 a 1, et ecrit l'octet correspondant.
bool parseConstant(const std::string& text, core::u8& out) {
    float value = 0.0f;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value);
    if (result.ec != std::errc() || result.ptr != last) {
        return false;
    }
    if (value < 0.0f || value > 1.0f) {
        return false;
    }
    out = static_cast<core::u8>(value * 255.0f + 0.5f);
    return true;
}

bool loadChannel(const std::string& argument, Channel& out) {
    if (parseConstant(argument, out.constant)) {
        out.isImage = false;
        return true;
    }
    if (!assets::loadImage(argument.c_str(), out.image)) {
        return false;
    }
    out.isImage = true;
    return true;
}

// Les entrees en niveaux de gris sont chargees en RGBA : les trois canaux sont egaux, on
// lit donc le rouge.
core::u8 sample(const Channel& channel, core::u32 index) {
    if (!channel.isImage) {
        return channel.constant;
    }
    return channel.image.pixels[static_cast<std::size_t>(index) * 4];
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string output = argv[1];
    std::string roughnessArg;
    std::string metalnessArg;
    std::string occlusionArg;

    for (int i = 2; i < argc; ++i) {
        const std::string flag = argv[i];
        if (i + 1 >= argc) {
            std::printf("valeur manquante apres %s\n", flag.c_str());
            return 1;
        }
        const std::string value = argv[++i];
        if (flag == "--roughness") {
            roughnessArg = value;
        } else if (flag == "--metalness") {
            metalnessArg = value;
        } else if (flag == "--ao") {
            occlusionArg = value;
        } else {
            std::printf("option inconnue : %s\n", flag.c_str());
            printUsage();
            return 1;
        }
    }

    if (roughnessArg.empty() || metalnessArg.empty()) {
        printUsage();
        return 1;
    }

    Channel roughness;
    Channel metalness;
    Channel occlusion;
    occlusion.constant = 255; // pas d'occlusion connue : la surface recoit tout

    if (!loadChannel(roughnessArg, roughness)) {
        std::printf("rugosite illisible : %s\n", roughnessArg.c_str());
        return 1;
    }
    if (!loadChannel(metalnessArg, metalness)) {
        std::printf("metallicite illisible : %s\n", metalnessArg.c_str());
        return 1;
    }
    if (!occlusionArg.empty() && !loadChannel(occlusionArg, occlusion)) {
        std::printf("occlusion illisible : %s\n", occlusionArg.c_str());
        return 1;
    }

    // Il faut au moins une image pour connaitre les dimensions : trois constantes ne
    // decriraient aucune taille.
    const Channel* reference = nullptr;
    for (const Channel* channel : {&roughness, &metalness, &occlusion}) {
        if (channel->isImage) {
            reference = channel;
            break;
        }
    }
    if (reference == nullptr) {
        std::printf("au moins une des entrees doit etre une image\n");
        return 1;
    }

    const core::u32 width = reference->image.width;
    const core::u32 height = reference->image.height;

    // Des tailles differentes donneraient une carte ou les canaux ne decrivent pas le meme
    // point de la surface : mieux vaut refuser que produire un fichier faux.
    for (const Channel* channel : {&roughness, &metalness, &occlusion}) {
        if (channel->isImage &&
            (channel->image.width != width || channel->image.height != height)) {
            std::printf("les entrees n'ont pas toutes la meme taille\n");
            return 1;
        }
    }

    std::vector<core::u8> packed(static_cast<std::size_t>(width) * height * 3);
    for (core::u32 i = 0; i < width * height; ++i) {
        packed[static_cast<std::size_t>(i) * 3 + 0] = sample(occlusion, i);
        packed[static_cast<std::size_t>(i) * 3 + 1] = sample(roughness, i);
        packed[static_cast<std::size_t>(i) * 3 + 2] = sample(metalness, i);
    }

    if (stbi_write_png(output.c_str(), static_cast<int>(width), static_cast<int>(height), 3,
                       packed.data(), static_cast<int>(width) * 3) == 0) {
        std::printf("ecriture impossible : %s\n", output.c_str());
        return 1;
    }

    std::printf("%s ecrit : %ux%u, R=occlusion G=rugosite B=metallicite\n", output.c_str(),
                width, height);
    return 0;
}
