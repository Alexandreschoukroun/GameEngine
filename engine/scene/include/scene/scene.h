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

    // Cree une entite avec un identifiant impose : le chargeur en a besoin pour restituer
    // les liens de parente du fichier.
    Entity createEntityWithId(std::string_view name, core::Uuid id);

    // Premiere entite portant ce nom, ou kInvalidEntity. Le nom n'est pas unique : c'est
    // une commodite pour le code de demonstration et les logs, pas une cle.
    Entity findByName(std::string_view name) const;

    // Attache child a parent : sa position devient relative a celle du parent.
    // Passer kInvalidEntity comme parent detache l'entite.
    // Refuse et journalise si le lien creerait un cycle - la passe de mise a jour est
    // recursive, un cycle ferait deborder la pile.
    bool setParent(Entity child, Entity parent);

    // Recalcule la matrice monde de chaque entite, parents avant enfants. A appeler une
    // fois par frame, avant tout systeme qui consomme des positions.
    //
    // Pas de drapeaux "sale" : ils seraient plus rapides, mais oublier d'invalider un
    // enfant donne un objet colle a son ancienne position, de facon intermittente. Le
    // SPEC interdit d'optimiser avant d'avoir mesure.
    void updateWorldTransforms();

    // Matrice monde d'une entite, telle que calculee par la derniere passe.
    core::Mat4 worldMatrix(Entity entity) const;

    // Acces direct au registre pour attacher des composants et parcourir des vues.
    // Les systemes travaillent dessus ; il n'y a aucune raison de le cacher.
    entt::registry& registry() { return m_registry; }
    const entt::registry& registry() const { return m_registry; }

private:
    core::Mat4 computeWorld(Entity entity, core::u32 epoch);

    entt::registry m_registry;
    core::u32 m_epoch = 0;
};

} // namespace scene
