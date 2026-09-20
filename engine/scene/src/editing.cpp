#include "scene/editing.h"

#include "core/uuid.h"
#include "scene/audio_sync.h"
#include "scene/components.h"
#include "scene/footsteps.h"
#include "scene/physics_sync.h"
#include "scene/sector_graph.h"

#include <algorithm>
#include <unordered_map>

namespace scene {
namespace {

// Copie un composant s'il existe. Le modele evite d'ecrire vingt fois le meme try_get.
template <typename Component>
void copyComponent(entt::registry& registry, Entity from, Entity to) {
    if (const Component* value = registry.try_get<Component>(from); value != nullptr) {
        registry.emplace_or_replace<Component>(to, *value);
    }
}

// Tous les composants DE DONNEES, ceux qui decrivent l'entite et partent dans le fichier.
//
// La liste est explicite, et c'est un cout a assumer : ajouter un composant au moteur
// oblige a l'ajouter ici, sous peine qu'une duplication le perde en silence. EnTT sait
// enumerer les types d'un registre, mais pas les copier sans qu'on les ait declares
// quelque part - le choix est donc entre cette liste et une machinerie de reflexion.
// Pour une vingtaine de composants, la liste gagne.
//
// Un test verrouille ce contrat : il duplique une entite qui les porte tous et verifie
// qu'aucun n'a ete oublie.
void copyDataComponents(entt::registry& registry, Entity from, Entity to) {
    copyComponent<Transform>(registry, from, to);
    copyComponent<Name>(registry, from, to);
    // Parent EN FAIT PARTIE : c'est une donnee, elle part dans le fichier. L'oublier
    // donnait une copie sans lien de parente, et la relier ensuite faisait echouer une
    // assertion d'EnTT - le test l'a signale avant qu'on ne lance le jeu.
    copyComponent<Parent>(registry, from, to);
    copyComponent<MeshRenderer>(registry, from, to);
    copyComponent<LightSource>(registry, from, to);
    copyComponent<Collider>(registry, from, to);
    copyComponent<Hinge>(registry, from, to);
    copyComponent<Surface>(registry, from, to);
    copyComponent<AudioSource>(registry, from, to);
    copyComponent<Sector>(registry, from, to);
    copyComponent<Portal>(registry, from, to);
}

} // namespace

void collectDescendants(const Scene& scene, Entity root, std::vector<Entity>& out) {
    out.clear();
    if (!scene.isValid(root)) {
        return;
    }
    // Parcours en largeur : on ajoute les enfants du dernier niveau trouve, jusqu'a ce
    // qu'un tour n'en ajoute plus. Un parcours recursif serait plus court, mais celui-ci
    // ne peut pas deborder la pile sur une hierarchie profonde.
    std::size_t examined = 0;
    out.push_back(root);
    while (examined < out.size()) {
        const Entity current = out[examined++];
        for (auto [candidate, parent] : scene.registry().view<const Parent>().each()) {
            if (parent.value != current) {
                continue;
            }
            // Un cycle aurait echappe a setParent, mais une scene peut venir d'un fichier
            // ecrit a la main : sans ce garde-fou, la boucle ne s'arreterait jamais.
            if (std::find(out.begin(), out.end(), candidate) == out.end()) {
                out.push_back(candidate);
            }
        }
    }
    // La racine n'est pas un descendant d'elle-meme.
    out.erase(out.begin());
}

Entity duplicateEntity(Scene& scene, Entity source) {
    if (!scene.isValid(source)) {
        return kInvalidEntity;
    }
    entt::registry& registry = scene.registry();

    std::vector<Entity> descendants;
    collectDescendants(scene, source, descendants);

    std::vector<Entity> originals;
    originals.reserve(descendants.size() + 1);
    originals.push_back(source);
    originals.insert(originals.end(), descendants.begin(), descendants.end());

    // On cree TOUTES les copies avant d'en relier aucune : un enfant peut apparaitre
    // avant son parent dans la liste, et il faut que sa copie existe deja pour etre
    // designee.
    std::unordered_map<std::uint32_t, Entity> mapping;
    mapping.reserve(originals.size());
    std::vector<Entity> copies;
    copies.reserve(originals.size());

    for (const Entity original : originals) {
        const Name* name = registry.try_get<Name>(original);
        // Un identifiant NEUF : c'est la cle du fichier de scene, deux entites ne peuvent
        // pas la partager sans que la sauvegarde devienne ambigue.
        const Entity copy =
            scene.createEntity(name != nullptr ? std::string_view(name->value)
                                               : std::string_view("entite"));
        copyDataComponents(registry, original, copy);
        mapping.emplace(entt::to_integral(original), copy);
        copies.push_back(copy);
    }

    for (std::size_t i = 0; i < originals.size(); ++i) {
        const Parent* parent = registry.try_get<Parent>(originals[i]);
        if (parent == nullptr) {
            continue;
        }
        const auto found = mapping.find(entt::to_integral(parent->value));
        if (found != mapping.end()) {
            // Parent interne a la copie : on relie a SA copie, sinon les deux arbres
            // partageraient des enfants.
            registry.get<Parent>(copies[i]).value = found->second;
        }
        // Parent exterieur : le lien copie tel quel est le bon. Dupliquer une poignee
        // seule doit donner une seconde poignee sur la meme porte.
    }

    return copies.front();
}

void destroyEntityTree(Scene& scene, Entity entity) {
    if (!scene.isValid(entity)) {
        return;
    }
    std::vector<Entity> descendants;
    collectDescendants(scene, entity, descendants);
    // Les descendants d'abord : detruire la racine en premier invaliderait les liens par
    // lesquels on les retrouve.
    for (auto it = descendants.rbegin(); it != descendants.rend(); ++it) {
        scene.destroyEntity(*it);
    }
    scene.destroyEntity(entity);
}

} // namespace scene
