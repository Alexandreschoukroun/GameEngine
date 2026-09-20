#pragma once

#include "core/types.h"
#include "scene/scene.h"

#include <memory>

namespace platform {
class Input;
class Window;
} // namespace platform

namespace editor {

// L'editeur, affiche PAR-DESSUS le jeu qui continue de tourner.
//
// C'est le choix structurant du jalon M6, et il merite d'etre explicite. Unity et Unreal
// separent l'editeur du jeu en deux processus, ce qui impose une serialisation permanente
// entre les deux et un mode "play" qui relance tout. Ici il n'y a jamais eu de separation
// a franchir : le jeu tourne deja, l'editeur se contente de le dessiner et de modifier ses
// donnees en place. Le "play-in-editor" que le SPEC reclame est donc gratuit.
//
// Dear ImGui travaille en MODE IMMEDIAT : on ne construit pas un arbre de widgets qu'on
// tient a jour, on redessine tout a chaque image. Pour un editeur dont les donnees changent
// sans cesse, c'est exactement le bon modele - il n'existe aucun etat d'interface a
// synchroniser avec la scene, donc aucun moyen que les deux divergent.
class Editor {
public:
    Editor();
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    // Branche l'interface sur la fenetre et sur le flux d'evenements. L'editeur s'abonne
    // aux evenements bruts : il a besoin du clavier, du texte et de la molette, que
    // l'InputState du moteur ne retient pas.
    bool create(platform::Window& window, platform::Input& input);
    void destroy();

    void setVisible(bool visible);
    void toggle();
    bool isVisible() const;

    // Vrai quand l'interface a la main sur la souris ou le clavier. Le jeu doit alors
    // ignorer ces entrees, sans quoi cliquer sur un bouton ferait aussi tourner la camera
    // et taper un nom d'entite ferait marcher le joueur.
    bool capturesMouse() const;
    bool capturesKeyboard() const;

    // Dessine l'interface et applique les modifications a la scene. A appeler une fois par
    // image, APRES le rendu de la scene : l'interface se pose par-dessus.
    //
    // La scene est passee par reference non const : editer, c'est modifier.
    void draw(scene::Scene& scene);

    // Entite selectionnee, ou kInvalidEntity. Le jeu s'en sert pour la mettre en evidence.
    scene::Entity selected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace editor
