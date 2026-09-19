#pragma once

#include "core/types.h"
#include "scene/components.h"

#include <entt/entity/registry.hpp>

#include <string_view>

namespace scene {

// Une entite n'est qu'un identifiant : aucune donnee, aucune methode. Ce qu'elle "est"
// depend entierement des composants qu'on lui attache.
using Entity = entt::entity;
inline constexpr Entity kInvalidEntity = entt::null;

// Le registre possede toutes les entites et tous leurs composants.
//
// Cette classe reste volontairement mince : elle ne fait qu'exposer EnTT avec le
// vocabulaire du moteur. L'enveloppe complete serait du travail pour rien, et EnTT est
// verrouille dans la stack du SPEC - il n'y aura pas de second ECS a substituer.
class Scene {
public:
    // Toute entite recoit un Transform : sans position, un objet n'est nulle part.
    Entity createEntity(std::string_view name);
    void destroyEntity(Entity entity);

    bool isValid(Entity entity) const { return m_registry.valid(entity); }

    // Acces direct au registre pour attacher des composants et parcourir des vues.
    // Les systemes travaillent dessus ; il n'y a aucune raison de le cacher.
    entt::registry& registry() { return m_registry; }
    const entt::registry& registry() const { return m_registry; }

private:
    entt::registry m_registry;
};

} // namespace scene
