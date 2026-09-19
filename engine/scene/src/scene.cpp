#include "scene/scene.h"

#include "core/log.h"
#include "core/uuid.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>

namespace scene {

core::Mat4 Transform::matrix() const {
    // Ordre classique : on met a l'echelle, puis on tourne, puis on translate. Lu de
    // droite a gauche dans le produit, comme toujours avec les matrices colonne.
    // Une seule conversion depuis le quaternion remplace les trois rotations successives
    // qu'exigeaient les angles d'Euler - et supprime la question de leur ordre.
    const core::Mat4 result = glm::translate(core::Mat4(1.0f), position) *
                              glm::mat4_cast(rotation);
    return glm::scale(result, scale);
}

core::Mat3 Transform::normalMatrix() const {
    return glm::transpose(glm::inverse(core::Mat3(matrix())));
}

Entity Scene::createEntity(std::string_view name) {
    const Entity entity = m_registry.create();
    // Tout objet a une place dans le monde : le Transform n'est pas optionnel.
    m_registry.emplace<Transform>(entity);
    m_registry.emplace<Name>(entity, std::string(name));
    // Identifiant stable des la creation : c'est lui qui survit a la sauvegarde.
    m_registry.emplace<Id>(entity, core::generateUuid());
    return entity;
}

Entity Scene::createEntityWithId(std::string_view name, core::Uuid id) {
    const Entity entity = m_registry.create();
    m_registry.emplace<Transform>(entity);
    m_registry.emplace<Name>(entity, std::string(name));
    m_registry.emplace<Id>(entity, id);
    return entity;
}

Entity Scene::findByName(std::string_view name) const {
    for (auto [entity, entityName] : m_registry.view<const Name>().each()) {
        if (entityName.value == name) {
            return entity;
        }
    }
    return kInvalidEntity;
}

bool Scene::setParent(Entity child, Entity parent) {
    if (!m_registry.valid(child)) {
        return false;
    }

    if (parent == kInvalidEntity) {
        m_registry.remove<Parent>(child);
        return true;
    }
    if (!m_registry.valid(parent) || parent == child) {
        core::logError("setParent : parent invalide");
        return false;
    }

    // Remonte la chaine du futur parent : si on y retrouve l'enfant, le lien fermerait une
    // boucle et la passe recursive ne se terminerait jamais.
    for (Entity ancestor = parent; ancestor != kInvalidEntity;) {
        if (ancestor == child) {
            core::logError("setParent refuse : le lien creerait un cycle");
            return false;
        }
        const Parent* link = m_registry.try_get<Parent>(ancestor);
        ancestor = link != nullptr ? link->value : kInvalidEntity;
    }

    m_registry.emplace_or_replace<Parent>(child, parent);
    return true;
}

core::Mat4 Scene::computeWorld(Entity entity, core::u32 epoch) {
    auto& world = m_registry.get_or_emplace<WorldTransform>(entity);
    if (world.epoch == epoch) {
        return world.matrix; // deja calculee cette frame
    }
    // Marque avant de recurser : en cas de cycle qui aurait echappe a setParent, on
    // s'arrete au lieu de deborder la pile.
    world.epoch = epoch;

    const Transform* local = m_registry.try_get<Transform>(entity);
    const core::Mat4 localMatrix = local != nullptr ? local->matrix() : core::Mat4(1.0f);

    const Parent* link = m_registry.try_get<Parent>(entity);
    if (link == nullptr || !m_registry.valid(link->value)) {
        world.matrix = localMatrix;
    } else {
        // Le parent d'abord : sa matrice monde multiplie celle de l'enfant.
        world.matrix = computeWorld(link->value, epoch) * localMatrix;
    }
    return world.matrix;
}

void Scene::updateWorldTransforms() {
    ++m_epoch;
    for (const Entity entity : m_registry.view<Transform>()) {
        computeWorld(entity, m_epoch);
    }
}

core::Mat4 Scene::worldMatrix(Entity entity) const {
    const WorldTransform* world = m_registry.try_get<WorldTransform>(entity);
    return world != nullptr ? world->matrix : core::Mat4(1.0f);
}

void Scene::destroyEntity(Entity entity) {
    if (m_registry.valid(entity)) {
        m_registry.destroy(entity);
    }
}

} // namespace scene
