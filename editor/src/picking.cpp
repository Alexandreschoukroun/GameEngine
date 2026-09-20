#include "editor/picking.h"

#include "scene/components.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {
namespace {

// Rayon de la sphere qui rend designables les entites sans geometrie. Assez grand pour
// qu'on l'attrape, assez petit pour ne pas masquer ce qui est derriere.
constexpr core::f32 kInvisibleRadius = 0.25f;

// En deca, une composante de direction est consideree nulle : le rayon est parallele a
// cette paire de faces, et la division qui suit produirait un infini.
constexpr core::f32 kEpsilon = 1e-6f;

} // namespace

bool rayIntersectsAabb(const Ray& ray, const core::Vec3& min, const core::Vec3& max,
                       core::f32& outDistance) {
    core::f32 enter = 0.0f;
    core::f32 exit = std::numeric_limits<core::f32>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const core::f32 direction = ray.direction[axis];
        const core::f32 origin = ray.origin[axis];
        if (std::abs(direction) < kEpsilon) {
            // Rayon parallele a cette paire de faces : il n'y entre jamais, donc soit il
            // est deja entre les deux, soit il les manque definitivement.
            if (origin < min[axis] || origin > max[axis]) {
                return false;
            }
            continue;
        }
        core::f32 t1 = (min[axis] - origin) / direction;
        core::f32 t2 = (max[axis] - origin) / direction;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        enter = std::max(enter, t1);
        exit = std::min(exit, t2);
        // Les intervalles ne se recouvrent plus : le rayon passe a cote.
        if (enter > exit) {
            return false;
        }
    }

    outDistance = enter;
    return true;
}

bool rayIntersectsSphere(const Ray& ray, const core::Vec3& center, core::f32 radius,
                         core::f32& outDistance) {
    const core::Vec3 toCenter = center - ray.origin;
    const core::f32 projection = glm::dot(toCenter, ray.direction);
    const core::f32 distanceSquared = glm::dot(toCenter, toCenter) - projection * projection;
    const core::f32 radiusSquared = radius * radius;
    if (distanceSquared > radiusSquared) {
        return false;
    }
    const core::f32 half = std::sqrt(radiusSquared - distanceSquared);
    const core::f32 near = projection - half;
    const core::f32 far = projection + half;
    if (far < 0.0f) {
        // La sphere est entierement derriere : on ne selectionne pas ce qu'on ne voit pas.
        return false;
    }
    // Depuis l'interieur, l'entree est derriere nous : la distance utile est zero.
    outDistance = near >= 0.0f ? near : 0.0f;
    return true;
}

scene::Entity pickEntity(const scene::Scene& scene, const scene::ResourceTable& resources,
                         const Ray& ray) {
    scene::Entity best = scene::kInvalidEntity;
    core::f32 bestDistance = std::numeric_limits<core::f32>::max();

    for (auto [entity, world] : scene.registry().view<const scene::WorldTransform>().each()) {
        const auto* mesh = scene.registry().try_get<const scene::MeshRenderer>(entity);
        const scene::MeshBounds bounds =
            mesh != nullptr ? resources.meshBounds(mesh->mesh) : scene::MeshBounds{};

        core::f32 distance = 0.0f;
        bool hit = false;
        if (bounds.valid) {
            // Les huit coins sont transformes puis reenglobes. La boite qui en resulte est
            // plus large que l'objet des qu'il est tourne - c'est le prix d'un test aligne
            // sur les axes, et il est sans consequence pour designer quelque chose.
            core::Vec3 worldMin(std::numeric_limits<core::f32>::max());
            core::Vec3 worldMax(std::numeric_limits<core::f32>::lowest());
            for (int corner = 0; corner < 8; ++corner) {
                const core::Vec3 local((corner & 1) != 0 ? bounds.max.x : bounds.min.x,
                                       (corner & 2) != 0 ? bounds.max.y : bounds.min.y,
                                       (corner & 4) != 0 ? bounds.max.z : bounds.min.z);
                const core::Vec3 point(world.matrix * core::Vec4(local, 1.0f));
                worldMin = glm::min(worldMin, point);
                worldMax = glm::max(worldMax, point);
            }
            hit = rayIntersectsAabb(ray, worldMin, worldMax, distance);
        } else {
            // Lumieres, sources sonores, secteurs : rien a montrer, mais il faut pouvoir
            // les atteindre.
            const core::Vec3 origin(world.matrix[3]);
            hit = rayIntersectsSphere(ray, origin, kInvisibleRadius, distance);
        }

        if (hit && distance < bestDistance) {
            bestDistance = distance;
            best = entity;
        }
    }
    return best;
}

} // namespace editor
