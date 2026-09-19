#include "scene/serialization.h"

#include "core/log.h"
#include "scene/resource_table.h"
#include "scene/scene.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace scene {
namespace {

// ordered_json conserve l'ordre d'insertion, la ligne au-dessus. Avec le json standard,
// les cles seraient triees par ordre alphabetique - deterministe aussi, mais illisible :
// "scale" apparaitrait avant "position".
using Json = nlohmann::ordered_json;

// Source de non-determinisme numero 3 : l'ecriture des flottants.
//
// Un float converti en double s'ecrit "0.34999999403953552". C'est exact, mais illisible
// dans un diff. On arrondit au micrometre : bien en dessous de tout ce qui a un sens dans
// un jeu, et l'operation est idempotente - relire puis reecrire donne le meme texte.
double rounded(core::f32 value) {
    const double scaled = std::round(static_cast<double>(value) * 1e6);
    // +0.0 normalise le zero negatif, qui s'ecrirait "-0.0" et polluerait les diffs.
    return scaled / 1e6 + 0.0;
}

Json toJson(const core::Vec3& v) {
    return Json::array({rounded(v.x), rounded(v.y), rounded(v.z)});
}

// Les identifiants sont ecrits en hexadecimal, comme chaines. Un entier 64 bits depasse la
// precision exacte des nombres JSON, que beaucoup d'outils lisent en double : un identifiant
// serait alors silencieusement modifie en passant par un formateur ou un editeur.
std::string toHex(core::Uuid id) {
    char buffer[19];
    std::snprintf(buffer, sizeof(buffer), "0x%016llx", static_cast<unsigned long long>(id));
    return std::string(buffer);
}

const char* lightTypeName(renderer::LightType type) {
    return type == renderer::LightType::Spot ? "spot" : "point";
}

} // namespace

std::string saveSceneToString(const Scene& scene, const ResourceTable& resources) {
    const entt::registry& registry = scene.registry();

    // Source de non-determinisme numero 1 : l'ordre de parcours du registre, qui depend de
    // l'ordre de creation et de destruction des entites. On trie par identifiant.
    std::vector<std::pair<core::Uuid, Entity>> sorted;
    for (auto [entity, id] : registry.view<const Id>().each()) {
        sorted.emplace_back(id.value, entity);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    Json root;
    root["version"] = kSceneFormatVersion;
    Json entities = Json::array();

    for (const auto& [uuid, entity] : sorted) {
        Json node;
        node["id"] = toHex(uuid);

        if (const Name* name = registry.try_get<Name>(entity); name != nullptr) {
            node["name"] = name->value;
        }

        // Le parent est designe par son identifiant, pas par son indice : le fichier reste
        // valide meme si l'ordre change.
        if (const Parent* parent = registry.try_get<Parent>(entity);
            parent != nullptr && registry.valid(parent->value)) {
            if (const Id* parentId = registry.try_get<Id>(parent->value);
                parentId != nullptr) {
                node["parent"] = toHex(parentId->value);
            }
        }

        if (const Transform* transform = registry.try_get<Transform>(entity);
            transform != nullptr) {
            Json t;
            t["position"] = toJson(transform->position);
            t["rotation"] = toJson(transform->rotation);
            t["scale"] = toJson(transform->scale);
            node["transform"] = t;
        }

        if (const MeshRenderer* mesh = registry.try_get<MeshRenderer>(entity);
            mesh != nullptr) {
            Json m;
            m["mesh"] = std::string(resources.meshName(mesh->mesh));
            m["baseColor"] = std::string(resources.textureName(mesh->baseColor));
            m["metallicRoughness"] =
                std::string(resources.textureName(mesh->metallicRoughness));
            node["mesh"] = m;
        }

        if (const LightSource* light = registry.try_get<LightSource>(entity);
            light != nullptr) {
            Json l;
            l["type"] = lightTypeName(light->type);
            l["color"] = toJson(light->color);
            l["intensity"] = rounded(light->intensity);
            l["innerAngle"] = rounded(light->innerAngleRadians);
            l["outerAngle"] = rounded(light->outerAngleRadians);
            l["range"] = rounded(light->range);
            l["castsShadow"] = light->castsShadow;
            node["light"] = l;
        }

        entities.push_back(node);
    }

    root["entities"] = entities;
    // Indentation a 2 espaces : un fichier lisible se relit dans un diff, ce qui est tout
    // l'interet d'une serialisation texte.
    return root.dump(2) + "\n";
}

bool saveSceneToFile(const Scene& scene, const ResourceTable& resources, const char* path) {
    const std::string contents = saveSceneToString(scene, resources);

    // Cree l'arborescence si besoin : un flux de sortie n'y pourvoit pas, il echouerait
    // silencieusement sur un dossier absent.
    std::error_code error;
    const std::filesystem::path full(path);
    if (full.has_parent_path()) {
        std::filesystem::create_directories(full.parent_path(), error);
    }

    // Binaire : sans ca, Windows traduirait chaque \n en \r\n et le fichier differerait
    // selon le systeme qui l'a ecrit - exactement le non-determinisme qu'on combat.
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        core::logError("impossible d'ecrire la scene");
        core::logError(path);
        return false;
    }
    file << contents;
    if (!file.good()) {
        core::logError("ecriture de la scene interrompue");
        return false;
    }
    core::logInfo("scene sauvegardee");
    core::logInfo(path);
    return true;
}

} // namespace scene
