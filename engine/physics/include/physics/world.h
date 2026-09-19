#pragma once

#include "core/math.h"
#include "core/types.h"

#include <memory>

namespace physics {

// Identifiant d'un corps physique. Un entier, pas un pointeur : les corps appartiennent au
// monde, et l'appelant n'a aucune raison de tenir une adresse sur les entrailles de Jolt.
using BodyHandle = core::u32;
inline constexpr BodyHandle kInvalidBody = 0xFFFFFFFFu;

// Monde physique.
//
// Toute la mecanique de Jolt - allocateur, systeme de taches, couches de collision,
// filtres - est enfermee dans l'implementation. Aucun type Jolt n'apparait dans cet
// en-tete : c'est la regle 2 du SPEC, et c'est ce qui permettra d'en changer un jour sans
// toucher au reste du moteur.
class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    bool create();
    void destroy();

    // A appeler a pas fixe. La physique n'est deterministe qu'a pas constant : c'est
    // precisement pour ca que la boucle de M0 a ete construite ainsi.
    void step(core::f32 fixedDeltaSeconds);

    // Un corps statique ne bouge jamais et ne coute presque rien : murs, sols, decor.
    // Un corps dynamique subit la gravite et les chocs.
    BodyHandle addBox(const core::Vec3& position, const core::Quat& rotation,
                      const core::Vec3& halfExtents, bool isStatic);

    core::Vec3 bodyPosition(BodyHandle body) const;
    core::Quat bodyRotation(BodyHandle body) const;

private:
    // PIMPL : la definition du monde Jolt vit dans le .cpp, donc les en-tetes de Jolt ne
    // se propagent nulle part.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace physics
