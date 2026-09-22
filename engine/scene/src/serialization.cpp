#include "scene/serialization.h"

#include "core/log.h"
#include "scene/resource_table.h"
#include "scene/scene.h"
#include "scene/audio_sync.h"
#include "scene/footsteps.h"
#include "scene/physics_sync.h"
#include "scene/sector_graph.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
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

// Ordre x, y, z, w : le scalaire en dernier, comme l'ecrit glTF. Autant suivre la
// convention du format qu'on charge deja, plutot que celle de la bibliotheque de maths.
Json toJson(const core::Quat& q) {
    return Json::array({rounded(q.x), rounded(q.y), rounded(q.z), rounded(q.w)});
}

core::Quat quatFromJson(const Json& node, const core::Quat& fallback) {
    if (!node.is_array() || node.size() != 4) {
        return fallback;
    }
    // Relu dans le meme ordre qu'ecrit, puis normalise : un quaternion legerement
    // denormalise par l'arrondi deformerait les objets.
    return glm::normalize(core::Quat(node[3].get<core::f32>(), node[0].get<core::f32>(),
                                     node[1].get<core::f32>(), node[2].get<core::f32>()));
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

core::Uuid fromHex(const std::string& text) {
    // strtoull accepte le prefixe 0x et s'arrete au premier caractere invalide : une
    // chaine malformee donne 0, que l'appelant traite comme un identifiant absent.
    return static_cast<core::Uuid>(std::strtoull(text.c_str(), nullptr, 16));
}

core::Vec3 vec3FromJson(const Json& node, const core::Vec3& fallback) {
    if (!node.is_array() || node.size() != 3) {
        return fallback;
    }
    return core::Vec3{node[0].get<core::f32>(), node[1].get<core::f32>(),
                      node[2].get<core::f32>()};
}

// Resout un nom de ressource, avec repli sur "missing". Un nom inconnu doit se VOIR : un
// objet silencieusement absent se diagnostique bien plus difficilement qu'un objet affiche
// avec une ressource de remplacement.
template <typename Finder>
ResourceHandle resolveResource(const Json& node, const char* key, Finder find,
                               const char* kind) {
    if (!node.contains(key) || !node[key].is_string()) {
        return kInvalidResource;
    }
    const std::string name = node[key].get<std::string>();
    const ResourceHandle handle = find(name);
    if (handle != kInvalidResource) {
        return handle;
    }
    core::logWarn("ressource inconnue, remplacement par missing");
    core::logWarn(kind);
    core::logWarn(name);
    return find(std::string("missing"));
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

    // L'environnement precede les entites : il decrit le cadre dans lequel elles vivent.
    {
        const Environment& environment = scene.environment();
        Json e;
        e["skyColor"] = toJson(environment.skyColor);
        e["groundColor"] = toJson(environment.groundColor);
        e["intensity"] = rounded(environment.intensity);
        e["exposureStops"] = rounded(environment.exposureStops);
        root["environment"] = e;
    }
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
            m["material"] = std::string(resources.materialName(mesh->material));
            node["mesh"] = m;
        }

        if (const AudioSource* source = registry.try_get<AudioSource>(entity);
            source != nullptr) {
            Json a;
            a["sound"] = std::string(resources.soundName(source->sound));
            a["volume"] = rounded(source->volume);
            a["looping"] = source->looping;
            node["audio"] = a;
        }

        if (const Surface* surface = registry.try_get<Surface>(entity);
            surface != nullptr) {
            Json f;
            f["footstep"] = std::string(resources.soundName(surface->footstep));
            node["surface"] = f;
        }

        if (const Collider* collider = registry.try_get<Collider>(entity);
            collider != nullptr) {
            Json c;
            // Une seule forme aujourd'hui, mais ecrite explicitement : ajouter la capsule
            // plus tard ne demandera pas de changer la version du format.
            c["shape"] = collider->shape == ColliderShape::Mesh ? "mesh" : "box";
            if (collider->shape == ColliderShape::Mesh) {
                c["collisionMesh"] =
                    std::string(resources.collisionMeshName(collider->collisionMesh));
            }
            c["halfExtents"] = toJson(collider->halfExtents);
            c["static"] = collider->isStatic;
            // Seuls les corps dynamiques ont une masse : l'ecrire pour un mur ne
            // decrirait rien et alourdirait le fichier.
            if (!collider->isStatic) {
                c["density"] = rounded(collider->density);
            }
            node["collider"] = c;
        }

        if (const Hinge* hinge = registry.try_get<Hinge>(entity); hinge != nullptr) {
            Json h;
            h["anchor"] = toJson(hinge->localAnchor);
            h["axis"] = toJson(hinge->axis);
            h["minAngle"] = rounded(hinge->minAngle);
            h["maxAngle"] = rounded(hinge->maxAngle);
            h["friction"] = rounded(hinge->friction);
            node["hinge"] = h;
        }

        if (const Sector* sector = registry.try_get<Sector>(entity); sector != nullptr) {
            Json s;
            s["halfExtents"] = toJson(sector->halfExtents);
            node["sector"] = s;
        }

        if (const Portal* portal = registry.try_get<Portal>(entity); portal != nullptr) {
            Json p;
            // Les secteurs sont designes par leur identifiant stable, comme les parents :
            // un rang dans le fichier ne survivrait pas a un tri ni a une fusion.
            const Id* a = registry.try_get<Id>(portal->sectorA);
            const Id* b = registry.try_get<Id>(portal->sectorB);
            p["sectorA"] = toHex(a != nullptr ? a->value : core::kInvalidUuid);
            p["sectorB"] = toHex(b != nullptr ? b->value : core::kInvalidUuid);
            p["halfExtents"] = toJson(portal->halfExtents);
            node["portal"] = p;
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

bool loadSceneFromString(Scene& scene, const ResourceTable& resources,
                         const std::string& json) {
    // nlohmann leve une exception sur un document malforme. Le SPEC interdit les
    // exceptions dans le moteur : on les arrete ici, a la frontiere du wrapper, et on les
    // convertit en code d'erreur. C'est exactement le cas prevu par la regle 6.
    Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded()) {
        core::logError("fichier de scene illisible : JSON malforme");
        return false;
    }

    const core::u32 version = root.value("version", 0u);
    if (version != kSceneFormatVersion) {
        // Refus net plutot qu'interpretation approximative : un niveau silencieusement
        // casse coute bien plus cher qu'un message clair.
        core::logError("version de scene incompatible");
        return false;
    }
    if (!root.contains("entities") || !root["entities"].is_array()) {
        core::logError("fichier de scene sans liste d'entites");
        return false;
    }

    // L'environnement est facultatif : une scene qui n'en declare pas garde les valeurs
    // par defaut, qui sont celles d'un interieur sombre.
    Environment environment;
    if (root.contains("environment")) {
        const Json& e = root["environment"];
        environment.skyColor = vec3FromJson(e.value("skyColor", Json()), environment.skyColor);
        environment.groundColor =
            vec3FromJson(e.value("groundColor", Json()), environment.groundColor);
        environment.intensity = e.value("intensity", environment.intensity);
        environment.exposureStops = e.value("exposureStops", environment.exposureStops);
    }

    // Tout ou rien : on construit a cote, et on ne remplace la scene de l'appelant qu'une
    // fois la lecture entierement reussie.
    Scene loaded;
    std::vector<std::pair<core::Uuid, Entity>> created;
    const Json& entities = root["entities"];

    // Premiere passe : creer toutes les entites. Un enfant peut etre ecrit AVANT son
    // parent, le fichier etant trie par identifiant et non par hierarchie : les liens ne
    // peuvent donc pas etre etablis tant que tout n'existe pas.
    for (const Json& node : entities) {
        if (!node.contains("id") || !node["id"].is_string()) {
            core::logError("entite sans identifiant");
            return false;
        }
        const core::Uuid id = fromHex(node["id"].get<std::string>());
        if (id == core::kInvalidUuid) {
            core::logError("identifiant d'entite invalide");
            return false;
        }
        const std::string name = node.value("name", std::string("entite"));
        created.emplace_back(id, loaded.createEntityWithId(name, id));
    }

    const auto findEntity = [&created](core::Uuid id) {
        for (const auto& pair : created) {
            if (pair.first == id) {
                return pair.second;
            }
        }
        return kInvalidEntity;
    };

    // Seconde passe : composants et liens de parente.
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Json& node = entities[i];
        const Entity entity = created[i].second;

        if (node.contains("transform")) {
            const Json& t = node["transform"];
            Transform& transform = loaded.registry().get<Transform>(entity);
            transform.position = vec3FromJson(t.value("position", Json()), transform.position);
            transform.rotation = quatFromJson(t.value("rotation", Json()), transform.rotation);
            transform.scale = vec3FromJson(t.value("scale", Json()), transform.scale);
        }

        if (node.contains("parent") && node["parent"].is_string()) {
            const Entity parent = findEntity(fromHex(node["parent"].get<std::string>()));
            if (parent == kInvalidEntity) {
                core::logError("parent introuvable dans le fichier de scene");
                return false;
            }
            if (!loaded.setParent(entity, parent)) {
                return false; // cycle : setParent a deja journalise
            }
        }

        if (node.contains("mesh")) {
            const Json& m = node["mesh"];
            MeshRenderer meshRenderer;
            meshRenderer.mesh = resolveResource(
                m, "mesh", [&](const std::string& n) { return resources.findMesh(n); },
                "maillage");
            // Pas de repli sur "missing" pour un materiau : il n'existe pas de matiere
            // de remplacement, et le maillage se replie deja sur le modele rose. Un nom
            // inconnu laisse donc l'entite sans matiere, et le rendu la saute.
            if (m.contains("material") && m["material"].is_string()) {
                const std::string name = m["material"].get<std::string>();
                meshRenderer.material = resources.findMaterial(name);
                if (meshRenderer.material == kInvalidResource) {
                    core::logWarn("materiau inconnu");
                    core::logWarn(name);
                }
            }
            loaded.registry().emplace<MeshRenderer>(entity, meshRenderer);
        }

        if (node.contains("audio")) {
            const Json& a = node["audio"];
            AudioSource source;
            // Pas de repli sur "missing" ici : il n'existe pas de son de remplacement, et
            // en jouer un a la place du bon serait pire que le silence. L'avertissement
            // suffit.
            if (a.contains("sound") && a["sound"].is_string()) {
                const std::string name = a["sound"].get<std::string>();
                source.sound = resources.findSound(name);
                if (source.sound == kInvalidResource) {
                    core::logWarn("son inconnu, source muette");
                    core::logWarn(name);
                }
            }
            source.volume = a.value("volume", source.volume);
            source.looping = a.value("looping", source.looping);
            loaded.registry().emplace<AudioSource>(entity, source);
        }

        if (node.contains("surface")) {
            const Json& f = node["surface"];
            Surface surface;
            if (f.contains("footstep") && f["footstep"].is_string()) {
                const std::string name = f["footstep"].get<std::string>();
                surface.footstep = resources.findSound(name);
                if (surface.footstep == kInvalidResource) {
                    core::logWarn("son de pas inconnu, surface muette");
                    core::logWarn(name);
                }
            }
            loaded.registry().emplace<Surface>(entity, surface);
        }

        if (node.contains("collider")) {
            const Json& c = node["collider"];
            Collider collider;
            collider.halfExtents =
                vec3FromJson(c.value("halfExtents", Json()), collider.halfExtents);
            collider.isStatic = c.value("static", collider.isStatic);
            collider.density = c.value("density", collider.density);
            if (c.value("shape", std::string("box")) == "mesh") {
                collider.shape = ColliderShape::Mesh;
                // Un maillage de triangles ne peut pas etre dynamique : Jolt le refuse, et
                // pour cause - il n'a ni volume ni masse bien definis.
                collider.isStatic = true;
                const std::string name = c.value("collisionMesh", std::string());
                collider.collisionMesh = resources.findCollisionMesh(name);
                if (collider.collisionMesh == kInvalidResource) {
                    core::logWarn("geometrie de collision inconnue");
                    core::logWarn(name);
                }
            }
            loaded.registry().emplace<Collider>(entity, collider);
        }

        if (node.contains("hinge")) {
            const Json& h = node["hinge"];
            Hinge hinge;
            hinge.localAnchor = vec3FromJson(h.value("anchor", Json()), hinge.localAnchor);
            hinge.axis = vec3FromJson(h.value("axis", Json()), hinge.axis);
            hinge.minAngle = h.value("minAngle", hinge.minAngle);
            hinge.maxAngle = h.value("maxAngle", hinge.maxAngle);
            hinge.friction = h.value("friction", hinge.friction);
            loaded.registry().emplace<Hinge>(entity, hinge);
        }

        if (node.contains("sector")) {
            Sector sector;
            sector.halfExtents =
                vec3FromJson(node["sector"].value("halfExtents", Json()), sector.halfExtents);
            loaded.registry().emplace<Sector>(entity, sector);
        }

        if (node.contains("portal")) {
            const Json& p = node["portal"];
            Portal portal;
            portal.sectorA = findEntity(fromHex(p.value("sectorA", std::string())));
            portal.sectorB = findEntity(fromHex(p.value("sectorB", std::string())));
            if (portal.sectorA == kInvalidEntity || portal.sectorB == kInvalidEntity) {
                core::logError("portail reliant un secteur introuvable");
                return false;
            }
            portal.halfExtents = vec3FromJson(p.value("halfExtents", Json()), portal.halfExtents);
            loaded.registry().emplace<Portal>(entity, portal);
        }

        if (node.contains("light")) {
            const Json& l = node["light"];
            LightSource light;
            light.type = l.value("type", std::string("point")) == "spot"
                             ? renderer::LightType::Spot
                             : renderer::LightType::Point;
            light.color = vec3FromJson(l.value("color", Json()), light.color);
            light.intensity = l.value("intensity", light.intensity);
            light.innerAngleRadians = l.value("innerAngle", light.innerAngleRadians);
            light.outerAngleRadians = l.value("outerAngle", light.outerAngleRadians);
            light.range = l.value("range", light.range);
            light.castsShadow = l.value("castsShadow", light.castsShadow);
            loaded.registry().emplace<LightSource>(entity, light);
        }
    }

    loaded.environment() = environment;
    scene = std::move(loaded);
    return true;
}

bool loadSceneFromFile(Scene& scene, const ResourceTable& resources, const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        core::logError("fichier de scene introuvable");
        core::logError(path);
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (!loadSceneFromString(scene, resources, contents.str())) {
        core::logError(path);
        return false;
    }
    core::logInfo("scene chargee");
    core::logInfo(path);
    return true;
}

} // namespace scene
