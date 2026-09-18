#include "assets/text_file.h"

#include "core/log.h"

#include <fstream>
#include <sstream>

namespace assets {

bool loadTextFile(const char* path, std::string& out) {
    // Ouverture en binaire : on ne veut aucune traduction de fins de ligne, le contenu
    // doit arriver tel qu'il est sur le disque.
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        core::logError("fichier texte introuvable");
        core::logError(path);
        return false;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        core::logError("lecture du fichier texte interrompue");
        core::logError(path);
        return false;
    }

    out = contents.str();
    return true;
}

} // namespace assets
