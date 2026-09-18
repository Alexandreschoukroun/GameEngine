#pragma once

#include <string>

namespace assets {

// Lit un fichier texte en entier. Sert aux sources de shaders, et servira aux scenes JSON.
// Rend false et journalise en cas d'echec : pas d'exception, conformement au SPEC.
bool loadTextFile(const char* path, std::string& out);

} // namespace assets
