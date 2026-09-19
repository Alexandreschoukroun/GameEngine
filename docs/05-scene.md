# 05 — Scène (M3)

*Brique 1 : les entités et les composants.*

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
