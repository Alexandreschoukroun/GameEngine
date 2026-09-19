#include "scene/physics_sync.h"

#include "core/log.h"

namespace scene {

void createPhysicsBodies(Scene& scene, physics::World& world) {
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

        const physics::BodyHandle handle =
            world.addBox(position, rotation, collider.halfExtents, collider.isStatic);
        if (handle == physics::kInvalidBody) {
            core::logError("creation du corps physique echouee pour une entite");
            continue;
        }
        registry.emplace<PhysicsBody>(entity, PhysicsBody{handle});
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
