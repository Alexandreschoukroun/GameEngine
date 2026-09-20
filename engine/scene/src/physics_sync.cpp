#include "scene/physics_sync.h"

#include "core/log.h"

namespace scene {

void createPhysicsBodies(Scene& scene, physics::World& world) {
    // Sans table, aucune geometrie de collision ne peut etre resolue : les colliders de
    // maillage seront signales et sautes, les boites fonctionnent normalement.
    const ResourceTable empty;
    createPhysicsBodies(scene, world, empty);
}

void createPhysicsBodies(Scene& scene, physics::World& world,
                         const ResourceTable& resources) {
    entt::registry& registry = scene.registry();

    // Les matrices monde doivent etre a jour : un collider peut etre enfant d'autre chose,
    // et c'est sa position dans le monde qui compte pour la simulation.
    scene.updateWorldTransforms();

    // On collecte avant de creer : ajouter un composant pendant qu'on parcourt sa vue
    // invaliderait le parcours.
    std::vector<Entity> pending;
    for (auto [entity, collider] : registry.view<const Collider>(entt::exclude<PhysicsBody>).each()) {
        (void)collider;
        pending.push_back(entity);
    }

    for (const Entity entity : pending) {
        const Collider& collider = registry.get<const Collider>(entity);
        const core::Mat4 world4 = scene.worldMatrix(entity);
        const core::Vec3 position(world4[3]);
        // La rotation vient du Transform local : extraire une orientation d'une matrice
        // qui porte aussi une echelle demanderait une decomposition, inutile tant que les
        // colliders ne sont pas mis a l'echelle.
        const Transform* transform = registry.try_get<Transform>(entity);
        const core::Quat rotation =
            transform != nullptr ? transform->rotation : core::Quat(1.0f, 0.0f, 0.0f, 0.0f);

        physics::BodyHandle handle = physics::kInvalidBody;
        if (collider.shape == ColliderShape::Mesh) {
            const CollisionMesh* geometry = resources.collisionMesh(collider.collisionMesh);
            if (geometry == nullptr || !geometry->isValid()) {
                core::logWarn("collider de maillage sans geometrie, entite sans collision");
                continue;
            }
            // L'echelle du Transform est transmise telle quelle : c'est elle qui permet
            // de reutiliser une meme geometrie a plusieurs tailles sans la dupliquer.
            const core::Vec3 scale =
                transform != nullptr ? transform->scale : core::Vec3{1.0f, 1.0f, 1.0f};
            handle = world.addMesh(position, rotation, scale, geometry->positions,
                                   geometry->vertexCount, geometry->indices,
                                   geometry->indexCount);
        } else {
            handle = world.addBox(position, rotation, collider.halfExtents,
                                  collider.isStatic, collider.density);
        }
        if (handle == physics::kInvalidBody) {
            core::logError("creation du corps physique echouee pour une entite");
            continue;
        }
        registry.emplace<PhysicsBody>(entity, PhysicsBody{handle});

        // La charniere vient apres le corps : elle l'accroche au monde.
        if (const Hinge* hinge = registry.try_get<Hinge>(entity); hinge != nullptr) {
            // L'ancrage est donne dans le repere de l'entite : on le passe en coordonnees
            // du monde, sans quoi toutes les portes pivoteraient autour de l'origine.
            const core::Vec3 anchor = core::Vec3(world4 * core::Vec4(hinge->localAnchor, 1.0f));
            const core::Vec3 axis = rotation * hinge->axis;
            world.addHinge(handle, anchor, axis, hinge->minAngle, hinge->maxAngle,
                           hinge->friction);
        }
    }
}

void syncPhysicsFromTransform(Scene& scene, physics::World& world, Entity entity) {
    if (!scene.isValid(entity)) {
        return;
    }
    const auto* body = scene.registry().try_get<PhysicsBody>(entity);
    if (body == nullptr) {
        return;
    }
    // La pose MONDE : un corps physique vit dans le monde, pas dans le repere de son
    // parent. Une poignee enfant d'une porte doit etre placee la ou on la voit.
    scene.updateWorldTransforms();
    const core::Mat4 world4 = scene.worldMatrix(entity);
    const core::Vec3 position(world4[3]);
    const auto* transform = scene.registry().try_get<Transform>(entity);
    const core::Quat rotation =
        transform != nullptr ? transform->rotation : core::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    world.setBodyTransform(body->handle, position, rotation);

    // Une charniere est ancree dans le MONDE : deplacer le battant sans la refaire le
    // laisserait pivoter autour de ses anciens gonds, et la contrainte le ramenerait de
    // force a sa place. C'est ce qui faisait revenir la porte des qu'on la lachait.
    if (const Hinge* hinge = scene.registry().try_get<Hinge>(entity); hinge != nullptr) {
        world.removeHinge(body->handle);
        const core::Vec3 anchor = core::Vec3(world4 * core::Vec4(hinge->localAnchor, 1.0f));
        world.addHinge(body->handle, anchor, rotation * hinge->axis, hinge->minAngle,
                       hinge->maxAngle, hinge->friction);
    }
}

void syncPhysicsFromTransforms(Scene& scene, physics::World& world) {
    // On collecte avant d'agir : refaire une charniere modifie les listes du monde
    // physique, pas celles de la scene, mais le principe de ne pas modifier ce qu'on
    // parcourt merite d'etre tenu partout.
    std::vector<Entity> bodies;
    for (auto [entity, body] : scene.registry().view<const PhysicsBody>().each()) {
        (void)body;
        bodies.push_back(entity);
    }
    for (const Entity entity : bodies) {
        syncPhysicsFromTransform(scene, world, entity);
    }
}

void syncTransformsFromPhysics(Scene& scene, const physics::World& world) {
    entt::registry& registry = scene.registry();

    for (auto [entity, body, collider, transform] :
         registry.view<const PhysicsBody, const Collider, Transform>().each()) {
        if (collider.isStatic || body.handle == physics::kInvalidBody) {
            continue; // un corps statique ne bouge pas : le recopier serait du travail perdu
        }
        transform.position = world.bodyPosition(body.handle);
        transform.rotation = world.bodyRotation(body.handle);
    }
}

} // namespace scene
