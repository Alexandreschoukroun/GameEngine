#pragma once

#include "core/math.h"
#include "core/types.h"

namespace editor {

// Axe manipule par le gizmo.
enum class GizmoAxis : core::u32 {
    None = 0,
    X = 1,
    Y = 2,
    Z = 3,
};

// Rayon partant de l'oeil et traversant la scene.
struct Ray {
    core::Vec3 origin{0.0f, 0.0f, 0.0f};
    core::Vec3 direction{0.0f, 0.0f, -1.0f};
};

// --- Geometrie ------------------------------------------------------------------------
//
// Ces quatre fonctions sont volontairement LIBRES et sans etat. C'est toute la difficulte
// d'un gizmo : traduire un mouvement de souris, qui vit en pixels, en un deplacement du
// monde, qui vit en metres. Les isoler les rend testables sans fenetre ni GPU, ce qui est
// la seule facon serieuse de verifier ce genre de calcul.

// Projette un point du monde vers les pixels de l'ecran.
//
// Rend FAUX si le point est derriere la camera : sa division perspective donnerait alors
// des coordonnees d'ecran parfaitement plausibles mais symetriques, et une poignee
// apparaitrait a l'oppose de l'objet.
bool worldToScreen(const core::Mat4& viewProjection, const core::Vec3& world,
                   const core::Vec2& viewport, core::Vec2& outScreen);

// Rayon passant par un pixel donne. C'est l'operation inverse de la precedente, et elle
// resservira a toute selection a la souris dans la vue.
Ray screenRay(const core::Mat4& inverseViewProjection, const core::Vec2& screen,
              const core::Vec2& viewport);

// Point de la droite (origine + t * direction) le plus proche du rayon.
//
// Rend FAUX quand les deux sont quasi paralleles : on regarde alors l'axe par la tranche,
// la solution part a l'infini, et il n'y a de toute facon aucun geste sense a faire.
bool closestPointOnAxis(const core::Vec3& axisOrigin, const core::Vec3& axisDirection,
                        const Ray& ray, core::f32& outT);

// Distance d'un point a un segment, en pixels. Sert au survol des poignees : une poignee
// est une ligne a l'ecran, pas un point.
core::f32 distanceToSegment(const core::Vec2& point, const core::Vec2& a,
                            const core::Vec2& b);

// --- Le gizmo lui-meme ------------------------------------------------------------------

// Manipulateur de translation : trois bras, un par axe.
//
// Il ne dessine rien et ne connait ni ImGui ni la scene. Il rend un DEPLACEMENT, a charge
// de l'appelant de l'appliquer - ce qui le laisse utilisable sur autre chose qu'un
// Transform, et testable sans rien afficher.
class TranslationGizmo {
public:
    struct Frame {
        core::Mat4 viewProjection{1.0f};
        core::Vec3 cameraPosition{0.0f, 0.0f, 0.0f};
        core::Vec2 mouse{0.0f, 0.0f};
        core::Vec2 viewport{1280.0f, 720.0f};
        // Vrai a l'image ou le bouton s'enfonce : c'est elle qui commence une saisie.
        bool mousePressed = false;
        // Vrai tant qu'il reste enfonce.
        bool mouseHeld = false;
    };

    // Avance d'une image et rend le deplacement a appliquer, en coordonnees du monde.
    // Nul tant qu'aucun bras n'est saisi.
    core::Vec3 update(const Frame& frame, const core::Vec3& origin);

    GizmoAxis hovered() const { return m_hovered; }
    GizmoAxis dragged() const { return m_dragged; }
    bool isDragging() const { return m_dragged != GizmoAxis::None; }

    // Longueur d'un bras dans le monde, pour la position d'objet donnee. Elle croit avec
    // la distance a la camera, pour que le gizmo garde une taille constante a l'ecran :
    // un manipulateur qui retrecit quand on recule devient inutilisable.
    static core::f32 armLength(const core::Vec3& cameraPosition, const core::Vec3& origin);

    // Direction d'un axe. Le gizmo travaille dans le repere du MONDE, pas dans celui de
    // l'objet : c'est le comportement par defaut de tous les editeurs, et le seul qui
    // permette d'aligner deux objets orientes differemment.
    static core::Vec3 axisDirection(GizmoAxis axis);

private:
    GizmoAxis m_hovered = GizmoAxis::None;
    GizmoAxis m_dragged = GizmoAxis::None;
    // Position le long de l'axe au moment de la saisie. La conserver evite que l'objet
    // ne saute sous le curseur : on a attrape le bras quelque part, pas en son milieu.
    core::f32 m_grabOffset = 0.0f;
};

} // namespace editor
