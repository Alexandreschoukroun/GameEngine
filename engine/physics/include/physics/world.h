#pragma once

#include "core/math.h"
#include "core/types.h"

#include <memory>

namespace physics {

// Identifiant d'un corps physique. Un entier, pas un pointeur : les corps appartiennent au
// monde, et l'appelant n'a aucune raison de tenir une adresse sur les entrailles de Jolt.
using BodyHandle = core::u32;
inline constexpr BodyHandle kInvalidBody = 0xFFFFFFFFu;

// Personnage controle par le joueur ou par l'IA. Il n'est pas un corps rigide ordinaire :
// il interroge le monde au lieu d'y etre simule, donc il ne bascule jamais et ne rebondit
// pas. C'est ce qui le rend pilotable.
using CharacterHandle = core::u32;
inline constexpr CharacterHandle kInvalidCharacter = 0xFFFFFFFFu;

// Monde physique.
//
// Toute la mecanique de Jolt - allocateur, systeme de taches, couches de collision,
// filtres - est enfermee dans l'implementation. Aucun type Jolt n'apparait dans cet
// en-tete : c'est la regle 2 du SPEC, et c'est ce qui permettra d'en changer un jour sans
// toucher au reste du moteur.
// Resultat d'un lancer de rayon. Il servira a la saisie d'objets, puis au champ de vision
// de l'antagoniste en M7 : voir, c'est lancer un rayon et regarder ce qu'il touche.
struct RayHit {
    BodyHandle body = kInvalidBody;
    core::Vec3 point{0.0f, 0.0f, 0.0f};
    core::Vec3 normal{0.0f, 1.0f, 0.0f};
    core::f32 distance = 0.0f;
    bool hit = false;
};

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

    // Vitesse d'un corps dynamique. L'imposer est la maniere la plus stable de deplacer
    // un objet tenu : il reste soumis aux collisions, donc il se coince dans une porte au
    // lieu de la traverser.
    void setBodyVelocity(BodyHandle body, const core::Vec3& linear);
    core::Vec3 bodyVelocity(BodyHandle body) const;

    // Vrai si le corps est dynamique et peut donc etre saisi ou pousse.
    bool isBodyDynamic(BodyHandle body) const;

    // Premier corps touche par le rayon. Le lancer depuis l'oeil du joueur donne l'objet
    // vise ; le lancer depuis les yeux d'un monstre donnera sa ligne de vue.
    RayHit raycast(const core::Vec3& origin, const core::Vec3& direction,
                   core::f32 maxDistance) const;

    // --- Personnages ------------------------------------------------------------------
    //
    // La position designe les PIEDS, pas le centre : c'est ce qu'on veut poser sur un sol,
    // et ca evite de se demander ou passe le milieu de la capsule.
    CharacterHandle addCharacter(const core::Vec3& feetPosition, core::f32 radius,
                                 core::f32 height);

    void setCharacterVelocity(CharacterHandle character, const core::Vec3& velocity);
    core::Vec3 characterVelocity(CharacterHandle character) const;
    core::Vec3 characterPosition(CharacterHandle character) const;

    // Vrai quand le personnage repose sur une surface praticable. Une pente trop raide ne
    // compte pas comme un sol : on y glisse.
    bool characterOnGround(CharacterHandle character) const;

    core::Vec3 gravity() const;

private:
    // PIMPL : la definition du monde Jolt vit dans le .cpp, donc les en-tetes de Jolt ne
    // se propagent nulle part.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace physics
