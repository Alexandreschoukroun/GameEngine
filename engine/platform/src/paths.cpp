#include "platform/paths.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <filesystem>

namespace platform {
namespace {

bool isDirectory(const std::filesystem::path& path) {
    std::error_code error; // version sans exception : le SPEC les interdit dans le moteur
    return std::filesystem::is_directory(path, error);
}

std::filesystem::path resolveAssetsRoot() {
    // 1. Variable d'environnement : permet de lancer le jeu sur un autre jeu de donnees
    //    sans le recompiler. SDL_getenv plutot que std::getenv, que MSVC deconseille.
    if (const char* fromEnvironment = SDL_getenv("GAMEENGINE_ASSETS")) {
        const std::filesystem::path candidate(fromEnvironment);
        if (isDirectory(candidate)) {
            return candidate;
        }
        core::logWarn("GAMEENGINE_ASSETS ne designe pas un dossier existant, ignoree");
    }

    // 2. A cote de l'executable : c'est la disposition du jeu distribue.
    if (const char* basePath = SDL_GetBasePath()) {
        const std::filesystem::path candidate = std::filesystem::path(basePath) / "assets";
        if (isDirectory(candidate)) {
            return candidate;
        }
    }

    // 3. Le dossier des sources, inscrit par CMake a la configuration.
#if defined(GAMEENGINE_SOURCE_ASSETS_DIR)
    {
        const std::filesystem::path candidate(GAMEENGINE_SOURCE_ASSETS_DIR);
        if (isDirectory(candidate)) {
            return candidate;
        }
    }
#endif

    core::logError("aucun dossier de donnees trouve");
    return {};
}

} // namespace

const std::string& assetsRoot() {
    // Resolu une seule fois : la reponse ne change pas en cours d'execution.
    static const std::string root = resolveAssetsRoot().string();
    return root;
}

std::string assetPath(std::string_view relativePath) {
    return (std::filesystem::path(assetsRoot()) / relativePath).string();
}

} // namespace platform
