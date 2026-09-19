#include "scene/scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>

namespace scene {

core::Mat4 Transform::matrix() const {
    // Ordre classique : on met a l'echelle, puis on tourne, puis on translate. Lu de
    // droite a gauche dans le produit, comme toujours avec les matrices colonne.
    core::Mat4 result = glm::translate(core::Mat4(1.0f), position);
    result = glm::rotate(result, rotation.y, core::Vec3{0.0f, 1.0f, 0.0f});
    result = glm::rotate(result, rotation.x, core::Vec3{1.0f, 0.0f, 0.0f});
    result = glm::rotate(result, rotation.z, core::Vec3{0.0f, 0.0f, 1.0f});
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
    return entity;
}

void Scene::destroyEntity(Entity entity) {
    if (m_registry.valid(entity)) {
        m_registry.destroy(entity);
    }
}

} // namespace scene
