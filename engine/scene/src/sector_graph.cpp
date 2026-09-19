#include "scene/sector_graph.h"

#include <algorithm>

namespace scene {
namespace {

bool contains(const core::Vec3& center, const core::Vec3& halfExtents,
              const core::Vec3& point) {
    // Six comparaisons, sans racine carree ni produit scalaire : c'est tout l'interet
    // d'une boite alignee sur les axes.
    return std::abs(point.x - center.x) <= halfExtents.x &&
           std::abs(point.y - center.y) <= halfExtents.y &&
           std::abs(point.z - center.z) <= halfExtents.z;
}

} // namespace

Entity sectorAt(const Scene& scene, const core::Vec3& position) {
    const entt::registry& registry = scene.registry();
    for (auto [entity, world, sector] :
         registry.view<const WorldTransform, const Sector>().each()) {
        // La matrice monde porte la position : un secteur enfant d'un ascenseur suivrait
        // l'ascenseur sans code supplementaire.
        const core::Vec3 center(world.matrix[3]);
        if (contains(center, sector.halfExtents, position)) {
            return entity;
        }
    }
    return kInvalidEntity;
}

void reachableSectors(const Scene& scene, Entity from, core::u32 maxDepth,
                      std::vector<Entity>& out) {
    out.clear();
    if (from == kInvalidEntity || !scene.isValid(from)) {
        return;
    }

    const entt::registry& registry = scene.registry();
    out.push_back(from);

    // Parcours en largeur. La file est le vecteur de sortie lui-meme : les elements deja
    // visites servent de file, ce qui evite une allocation de plus.
    std::vector<core::u32> depths{0};
    for (std::size_t head = 0; head < out.size(); ++head) {
        const core::u32 depth = depths[head];
        if (depth >= maxDepth) {
            continue;
        }
        const Entity current = out[head];

        for (auto [portalEntity, portal] : registry.view<const Portal>().each()) {
            // Un portail relie deux secteurs : on prend celui qui n'est pas le courant.
            Entity neighbour = kInvalidEntity;
            if (portal.sectorA == current) {
                neighbour = portal.sectorB;
            } else if (portal.sectorB == current) {
                neighbour = portal.sectorA;
            }
            if (neighbour == kInvalidEntity || !registry.valid(neighbour)) {
                continue;
            }
            if (std::find(out.begin(), out.end(), neighbour) != out.end()) {
                continue; // deja atteint, par un chemin au plus aussi court
            }
            out.push_back(neighbour);
            depths.push_back(depth + 1);
        }
    }
}

} // namespace scene
