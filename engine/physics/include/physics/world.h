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
    //
    // La masse volumique est en kg/m3. Jolt suppose 1000 par defaut - celle de l'eau, donc
    // un solide plein. Une porte de 0,9 x 2 x 0,08 m pesait ainsi 144 kg, et refusait de
    // bouger. Le bois creux d'une vraie porte tourne autour de 150.
    BodyHandle addBox(const core::Vec3& position, const core::Quat& rotation,
                      const core::Vec3& halfExtents, bool isStatic,
                      core::f32 density = 1000.0f);

    // Corps dont la collision suit exactement une geometrie de triangles. C'est ce qui
    // permet de faire collisionner un NIVEAU entier - un couloir, un escalier, une piece
    // aux murs obliques - au lieu de l'approcher par des dizaines de boites a la main.
    //
    // Obligatoirement STATIQUE : un maillage de triangles n'a ni volume ni masse bien
    // definis, et aucun moteur physique ne sait faire rouler ca. Le decor ne bouge pas,
    // c'est donc exactement ce qu'il faut - et ce que Jolt impose.
    //
    // Les sommets sont donnes dans le repere du modele ; l'echelle est appliquee par le
    // moteur. Une echelle negative retournerait les triangles et la collision partirait
    // a l'envers : elle est refusee.
    BodyHandle addMesh(const core::Vec3& position, const core::Quat& rotation,
                       const core::Vec3& scale, const core::Vec3* vertices,
                       core::u32 vertexCount, const core::u32* indices,
                       core::u32 indexCount);

    core::Vec3 bodyPosition(BodyHandle body) const;
    core::Quat bodyRotation(BodyHandle body) const;

    // Vitesse d'un corps dynamique. L'imposer est la maniere la plus stable de deplacer
    // un objet tenu : il reste soumis aux collisions, donc il se coince dans une porte au
    // lieu de la traverser.
    void setBodyVelocity(BodyHandle body, const core::Vec3& linear);
    core::Vec3 bodyVelocity(BodyHandle body) const;

    // Vrai si le corps est dynamique et peut donc etre saisi ou pousse.
    bool isBodyDynamic(BodyHandle body) const;

    // Vitesse de rotation d'un corps, en radians par seconde autour de chaque axe. Sert
    // a AMORTIR : sans terme de vitesse, une force repetee chaque pas accelererait un
    // objet indefiniment.
    core::Vec3 bodyAngularVelocity(BodyHandle body) const;

    // Applique une impulsion EN UN POINT du corps. Appliquee hors du centre de masse,
    // elle cree un couple : c'est ce qui fait pivoter une porte quand on tire sur sa
    // poignee, et non quand on pousse en son milieu.
    void applyImpulseAtPoint(BodyHandle body, const core::Vec3& impulse,
                             const core::Vec3& point);

    // Charniere : bloque toutes les libertes du corps sauf une rotation autour d'un axe,
    // entre deux butees. C'est une porte, un couvercle, un volet.
    //
    // Les angles sont en radians, relatifs a la pose du corps au moment de la creation.
    bool addHinge(BodyHandle body, const core::Vec3& anchorPoint, const core::Vec3& axis,
                  core::f32 minAngle, core::f32 maxAngle, core::f32 friction);

    // Vrai si le corps est tenu par une charniere. L'appelant s'en sert pour choisir
    // comment le manipuler : on ne tire pas sur une porte comme sur une caisse libre.
    bool isBodyHinged(BodyHandle body) const;

    // Un objet tenu cesse de heurter le personnage, tout en continuant de heurter les
    // murs et les autres objets. Sans ca, ramener une caisse contre soi la ferait pousser
    // le porteur - et comme elle est pilotee a vitesse imposee, elle le propulserait.
    void setBodyHeld(BodyHandle body, bool held);

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

    // Change la taille du personnage, les pieds restant au sol - c'est ce qui permet de
    // s'accroupir et de se relever.
    //
    // Rend FAUX si le decor s'y oppose : se relever sous un plafond bas ferait entrer la
    // capsule dans la geometrie, et le personnage serait ejecte ou traverserait. Le refus
    // est donc la reponse juste, et c'est a l'appelant de rester accroupi.
    bool setCharacterHeight(CharacterHandle character, core::f32 height);

    // Hauteur actuelle, en metres.
    core::f32 characterHeight(CharacterHandle character) const;

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
