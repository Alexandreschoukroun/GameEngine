# 05 — Scène (M3)

*Brique 1 : les entités et les composants. Brique 2 : la hiérarchie (section 2).*

Le jalon M3 fait passer le moteur des variables membres aux **données** : entités, hiérarchie, sérialisation JSON, et le graphe de secteurs/portails. C'est le jalon qui rend un niveau chargeable depuis un fichier.

# 1. Brique 1 — entités et composants

## 1.1 Le problème

La scène était écrite en dur dans `main.cpp` : un maillage par variable membre, des positions en constantes. Pour ajouter un deuxième objet, on dupliquait quatre lignes ; pour cent, c'était hors de question. Et surtout, **rien ne pouvait venir d'un fichier**.

Plus fondamental encore : **les objets n'avaient pas de position**. La géométrie sortait du chargeur glTF en coordonnées du monde, figée à l'origine. Impossible de la déplacer, impossible d'en avoir deux exemplaires.

## 1.2 Les options considérées

**Une hiérarchie de classes** — `GameObject`, dont héritent `Door`, `Monster`, `Lamp`. C'est l'approche intuitive, celle des moteurs des années 2000. Elle s'effondre sur les cas mixtes : une porte qui émet de la lumière et du son devrait hériter de trois classes. En pratique, on finit avec une classe de base qui contient tout pour tout le monde.

**Des tableaux parallèles** — un tableau de positions, un de maillages, indexés pareil. Rapide, mais création, destruction et recherche sont entièrement à la main.

**Un ECS** — une entité est un **identifiant**, les données sont des **composants** attachés à la carte, les traitements sont des **systèmes** qui parcourent « toutes les entités ayant tel et tel composant ». Une porte lumineuse et bruyante est simplement une entité à quatre composants. Le parcours est rapide parce que les composants de même type sont contigus en mémoire.

**Choix : EnTT**, verrouillé par le SPEC, et référence du domaine en C++.

## 1.3 La décision de conception qui compte

Une lumière a une position. Un maillage aussi. **Où vit-elle ?**

Réponse : **uniquement dans le composant `Transform`**. Le composant `LightSource` ne contient que couleur, intensité, cône et portée — pas de position, pas de direction. Sinon deux vérités coexistent pour une même information, et un jour elles divergent.

Conséquence agréable : le jour où une lampe sera vissée à une porte qui s'ouvre, elle suivra sans une ligne de code supplémentaire.

## 1.4 Ce que ça imposait au renderer

Si les entités ont une position, le renderer doit savoir dessiner ailleurs qu'à l'origine. La **matrice modèle** entre donc dans le chemin de rendu, accompagnée de sa **matrice de normales** — la transposée de l'inverse, pour la raison déjà rencontrée dans le chargeur glTF.

Détail qui aurait pu coûter cher : **la passe d'ombre applique la même matrice**. Sans ça, les ombres seraient restées là où le fichier avait laissé le modèle, pendant que les objets, eux, se seraient déplacés.

## 1.5 Vocabulaire

- **Entité** : un identifiant, rien d'autre. Pas d'objet, pas de méthode.
- **Composant** : une donnée attachée à une entité. Des champs, aucun comportement.
- **Système** : un traitement qui parcourt les entités possédant certains composants. Dans `onRender`, il y en a maintenant deux — un pour les objets à dessiner, un pour les lumières.
- **Vue** (*view*) : la requête « toutes les entités ayant un `Transform` et un `MeshRenderer` ».
- **Registre** : le conteneur qui possède toutes les entités et leurs composants.

## 1.6 Une troisième exception à la règle 2

`EnTT::EnTT` est lié en **`PUBLIC`** à `engine_scene` : `entt::entity` et `entt::registry` apparaissent dans les en-têtes de la couche. C'est la troisième et dernière exception assumée, après GLM et Tracy, et pour la même raison — la stack du SPEC verrouille EnTT, il n'y aura pas de second ECS à substituer. Envelopper toute son API coûterait cher pour une indépendance dont on n'a aucun usage.

## 1.7 Coût

EnTT est une bibliothèque d'en-têtes : rien à l'exécution hors ce qu'on utilise. Créer une entité coûte quelques nanosecondes. Le gain est dans le parcours : mille objets traités par composants contigus, plutôt que mille pointeurs dispersés.

Les listes transmises au renderer sont des `std::vector` **réutilisés d'une frame à l'autre** — vidés sans être libérés. Aucune allocation dans la boucle de frame une fois le régime établi, conformément à la règle 7 du SPEC.

## 1.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : deux exemplaires du même maillage, à deux positions, deux échelles et deux orientations, plus la pièce et la lumière d'ambiance — tous devenus des entités. La lampe torche reste à part : elle appartient au joueur, pas à la scène.

**Ce qui n'existe pas encore** : aucune hiérarchie, donc aucun objet ne peut être attaché à un autre. La scène se construit toujours par du code, pas depuis un fichier. Rien ne détruit ni ne recharge une scène en cours de partie.

**Ce qui vient après (brique 2)** : la hiérarchie de transforms — un parent, des enfants, et la propagation des transformations.

---

# 2. Brique 2 — la hiérarchie de transforms

## 2.1 Le problème

Chaque entité avait une position **absolue**. Suffisant pour poser des statues, insuffisant pour ce que le jeu demandera : une poignée attachée à une porte qui doit tourner avec elle, une lampe posée sur une table qu'on déplace, plus tard une arme dans la main d'un personnage.

Dans tous ces cas, l'enfant est positionné **relativement au parent**, et le monde doit recalculer sa place quand le parent bouge.

## 2.2 Les options considérées

**Recalculer à la demande** : chaque objet remonte la chaîne de ses parents au moment où l'on a besoin de sa position. Aucune invalidation à gérer, mais le calcul se répète pour chaque consommateur — rendu, physique et audio referaient le même travail.

**Cacher avec des drapeaux « sale »** : mémoriser la matrice monde et ne la recalculer qu'en cas de changement. Le plus rapide, et de loin le plus piégeux : oublier d'invalider un enfant donne un objet collé à son ancienne position, de façon intermittente — donc difficile à reproduire.

**Une passe de mise à jour par frame** : parcourir tout, parents avant enfants, écrire la matrice monde de chacun. Coût linéaire, aucune invalidation possible, un seul endroit où ça se passe.

**Choix : la passe par frame.** À l'échelle actuelle le coût est invisible, et le SPEC interdit d'optimiser avant d'avoir mesuré. Le jour où Tracy montrera que cette passe pèse, on ajoutera des drapeaux — avec des chiffres pour justifier la complexité.

## 2.3 Deux protections contre le débordement de pile

**Le cycle est refusé à la source.** Attacher un parent à son propre descendant ferait boucler le calcul récursif indéfiniment. `setParent` remonte donc la chaîne du futur parent et refuse le lien s'il y trouve l'enfant.

**Et le calcul se protège quand même.** Chaque entité est marquée comme « calculée cette frame » **avant** de récurser vers son parent. Si un cycle échappait malgré tout à la vérification, la passe s'arrêterait au lieu de faire exploser la pile. Un débordement de pile ne laisse aucune trace exploitable : la ceinture et les bretelles se justifient.

## 2.4 Le champ `epoch`

Le composant `WorldTransform` porte un compteur de frame. Il sert de mémo pendant la passe : une entité déjà calculée n'est pas recalculée, même si dix enfants la réclament comme parent. Le coût reste linéaire quelle que soit la forme de l'arbre, sans allouer de structure temporaire.

## 2.5 Vocabulaire

- **Transform local** : la position relative au parent. C'est ce qu'on règle.
- **Transform monde** : la position absolue, calculée. C'est ce que le rendu consomme.
- **Racine** : une entité sans parent — son local *est* son monde.
- **Propagation** : le fait qu'une modification du parent descende à toute sa descendance.

## 2.6 Ce que la démonstration montre

La statue tourne lentement sur elle-même, à pas fixe puisque c'est de la simulation. Deux entités lui sont attachées : un satellite et une braise.

Aucune des deux n'est touchée par le code de rotation. Pourtant le satellite orbite, et la braise **déplace l'éclairage de toute la pièce** en orbitant. C'est toute la hiérarchie en une ligne :

```cpp
transform.rotation.y += 0.35f * fixedDeltaSeconds;
```

Que la propagation se voie sur la **lumière** et pas seulement sur la géométrie est délibéré : c'est la preuve que la position d'une lumière vient bien de sa matrice monde, et non d'un champ dupliqué dans son composant.

## 2.7 Coût

Une multiplication de matrices par entité et par frame, plus un parcours linéaire. Invisible à cette échelle, et mesurable dans Tracy le jour où la scène grossira.

## 2.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : la rotation du parent entraîne ses deux enfants, y compris l'éclairage qu'ils portent.

**Ce qui n'existe pas encore** : détruire un parent laisse ses enfants avec un lien vers une entité invalide — le calcul les traite alors comme des racines, ce qui est acceptable mais n'a pas été décidé. La scène se construit toujours par du code.

**Ce qui vient après (brique 3)** : la sérialisation JSON déterministe. C'est le basculement du jalon — les niveaux cesseront d'être écrits en C++ pour devenir des fichiers.
