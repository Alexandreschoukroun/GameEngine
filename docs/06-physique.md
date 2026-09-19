# 06 — Physique (M4)

*Brique 1 : Jolt intégré, monde physique. Brique 2 : les colliders deviennent des données (section 2). Brique 3 : le contrôleur de personnage (section 3).*

Le jalon M4 apporte la physique : colliders, contrôleur de personnage, saisie d'objets et portes. Cette première brique pose le socle — et force au passage une décision restée en suspens depuis M3.

# 1. Brique 1 — le monde physique

## 1.1 Le problème

Rien ne tombe, rien ne se heurte. Les objets sont posés là où le fichier les décrit, et la caméra traverse les murs. Pour un jeu où l'on manipule des portes et des tiroirs à la main, c'est le manque le plus fondamental.

## 1.2 La décision que la physique a forcée

Le composant `Transform` stockait une rotation en **angles d'Euler**, avec ce commentaire écrit en M3 :

> « Un quaternion serait plus robuste pour des rotations composées ; on y viendra si le besoin apparaît, pas avant. »

Le besoin est apparu. Un moteur physique produit des **quaternions** : c'est la seule représentation qui compose proprement, s'interpole sans à-coup et ne souffre pas du **blocage de cardan** — cette perte d'un degré de liberté quand deux axes d'Euler s'alignent. Une caisse qui bascule sur une arête produit une orientation qu'aucun triplet d'angles ne décrit sans ambiguïté.

**Choix retenu : le quaternion partout**, y compris dans les fichiers, où il s'écrit en quatre nombres `[x, y, z, w]` — l'ordre de glTF, qu'on charge déjà.

Les deux alternatives ont été écartées pour des raisons précises :
- *quaternion en mémoire, degrés dans le fichier* : la conversion n'est pas exacte dans les deux sens, donc sauvegarder-recharger-resauvegarder changerait les dernières décimales. Cela casserait la garantie de diff minimal établie en M3, et le test qui la vérifie ;
- *garder les angles d'Euler* : il faudrait convertir à chaque frame, avec perte, et les objets basculant près des angles droits auraient un comportement erratique.

L'éditeur de M6 affichera des degrés tout en stockant un quaternion, comme Unity et Unreal.

Effet de bord agréable : le calcul de la matrice s'est simplifié. Trois rotations successives — et la question de leur ordre — remplacées par une seule conversion.

## 1.3 Ce que Jolt impose, et ce qu'on en cache

Jolt demande un allocateur, un système de tâches, deux classifications de couches et leurs filtres. Tout cela est enfermé dans l'implémentation : **aucun type Jolt n'apparaît dans l'en-tête public**, qui ne parle que de `Vec3`, de `Quat` et d'identifiants entiers. C'est la règle 2 du SPEC, appliquée à la dépendance la plus envahissante de la stack.

Les **deux classifications** méritent une explication, parce qu'elles reviennent dans tous les moteurs physiques :

- les **couches d'objet** disent qui peut heurter qui. Deux murs n'ont aucune raison d'être testés l'un contre l'autre ;
- les **couches de phase large** regroupent les corps dans l'arbre spatial. Les statiques, qui ne bougent jamais, y sont séparés des mobiles pour que leur arbre ne soit jamais reconstruit.

Séparer statique et dynamique n'est donc pas une commodité : c'est ce qui rend un décor de plusieurs milliers de murs gratuit.

## 1.4 Le pas fixe trouve enfin sa justification

La physique n'est déterministe qu'à **pas constant**. La boucle construite en M0 — accumulateur, pas de 1/60 s, plafond contre la spirale de la mort — existait précisément pour ce moment. `step()` n'est appelé que depuis `onFixedUpdate`, jamais depuis le rendu.

Un test le vérifie : deux simulations identiques, lancées séparément, donnent le même résultat.

## 1.5 Le sens de la synchronisation

La simulation fait autorité sur la position d'un objet dynamique. Chaque pas fixe : la physique avance, **puis** les entités recopient la pose de leur corps. Jamais l'inverse — écrire dans un `Transform` géré par la physique donnerait un objet qui tremble, tiraillé entre deux vérités.

## 1.6 Vocabulaire

- **Corps statique** : ne bouge jamais, ne subit pas la gravité. Murs, sols, décor.
- **Corps dynamique** : subit la gravité et les chocs.
- **Phase large** (*broad phase*) : le tri spatial qui élimine d'emblée les paires trop éloignées pour se toucher.
- **Blocage de cardan** (*gimbal lock*) : la perte d'un degré de liberté quand deux axes d'une rotation d'Euler s'alignent.
- **Quaternion** : quatre nombres décrivant une rotation, sans singularité.

## 1.7 Coût

Jolt alloue 16 Mo de mémoire temporaire et un thread par cœur moins un. Le pas de simulation, pour cinq caisses, ne se mesure pas. La vraie charge viendra avec le décor et le contrôleur de personnage — et c'est le poste CPU de 3,5 ms du budget du SPEC, partagé avec l'audio et l'IA.

## 1.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : cinq caisses lâchées en l'air tombent, basculent, se heurtent et s'immobilisent sur le sol.

**Quatre tests sans GPU**, donc exécutés en CI : chute libre à la bonne échelle, corps statique immobile, repos sur un sol à la bonne hauteur, et déterminisme de deux simulations identiques.

**Ce qui n'existe pas encore** : les colliders sont créés en code, pas décrits dans la scène. La correspondance entité ↔ corps est une liste tenue à la main. Le décor n'a pas de collision — seul un sol invisible existe. Et la caméra traverse toujours tout.

**Ce qui vient après (brique 2)** : les colliders décrits comme composants, donc sérialisés et éditables comme le reste de la scène.

---

# 2. Brique 2 — les colliders deviennent des données

## 2.1 Le problème

Les caisses étaient créées par du code, et la correspondance entité ↔ corps physique tenue dans une liste au fond du jeu. Le décor, lui, n'avait aucune collision : un sol invisible avait été posé à la main, et les murs n'existaient pas pour la simulation.

Tout cela devait rejoindre le fichier de scène, comme la géométrie et les lumières avant lui.

## 2.2 Deux composants, et pourquoi ils sont séparés

**`Collider`** décrit la collision : forme, demi-dimensions, statique ou dynamique. C'est de la **donnée**, elle part dans le fichier.

**`PhysicsBody`** contient l'identifiant du corps créé dans le moteur physique. Il n'est **jamais sérialisé** : un identifiant de corps n'existe qu'à l'exécution et change à chaque lancement. Un test le vérifie explicitement.

Cette séparation entre *ce qui décrit* et *ce qui vit à l'exécution* reviendra partout : elle est la même que celle entre le nom d'une ressource et sa poignée.

## 2.3 Une couche qui en appelle une autre

Le système qui relie la scène à la physique vit dans la couche `scene`, qui dépend donc de `physics`. C'est conforme au SPEC, qui place scène, physique, audio et IA dans la **même bande** — ces systèmes se parlent par nature.

Ce qui reste interdit, c'est qu'un type Jolt remonte : la frontière est tenue par le PIMPL de la brique 1, et l'API de `physics` ne parle que de `Vec3`, de `Quat` et d'entiers.

## 2.4 Collision et rendu sont deux choses différentes

Le maillage de la pièce est **creux, tourné vers l'intérieur** : il ne peut pas servir de collider tel quel. La collision du décor est donc décrite par six boîtes statiques qui doublent les murs visibles.

Ce n'est pas un contournement, c'est la pratique universelle : la géométrie de collision est toujours plus grossière que celle du rendu. Un mur sculpté de mille triangles se heurte très bien avec une boîte, et le gain de performance est considérable.

## 2.5 Le mécanisme de repli a fait son travail

Au premier essai, la console a affiché :

```
WARN | ressource inconnue, remplacement par missing
WARN | maillage
WARN | caisse
```

Le fichier réclamait le maillage `caisse`, enregistré **après** le chargement de la scène. Le repli sur `missing` — posé à la brique 3b de M3 — a transformé une erreur silencieuse en message immédiat. Sans lui, les caisses auraient simplement été absentes, et j'aurais cherché du côté de la physique.

Correctif : enregistrer les ressources avant de charger la scène.

## 2.6 Un piège d'ECS

Ajouter un composant pendant qu'on parcourt la vue qui le filtre invalide le parcours. `createPhysicsBodies` collecte donc d'abord les entités concernées, puis crée les corps dans une seconde boucle.

La vue exclut par ailleurs les entités qui ont déjà un `PhysicsBody` : sans ce filtre, chaque appel ajouterait un corps de plus au même endroit. Un test appelle la fonction deux fois et vérifie que le compte ne bouge pas.

## 2.7 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : les caisses viennent entièrement du fichier — maillage, échelle et collision — tombent et s'immobilisent, sans aucun avertissement.

**Trois tests** s'ajoutent : aller-retour d'un collider par le fichier, création idempotente des corps, et synchronisation qui ne touche que les corps dynamiques. **31 tests, 82 assertions** au total.

**Ce qui n'existe pas encore** : une seule forme de collision, la boîte. Les colliders ignorent l'échelle du `Transform`. Et surtout, **la caméra traverse toujours tout** : elle n'a pas de corps.

**Ce qui vient après (brique 3)** : le contrôleur de personnage — une capsule, la gravité, et des murs qui arrêtent enfin le joueur.

---

# 3. Brique 3 — le contrôleur de personnage

## 3.1 Pourquoi un personnage n'est pas une caisse

La caméra volait librement et traversait tout. La remplacer par un corps rigide ordinaire ne suffirait pas, parce qu'un joueur se comporte autrement qu'un objet :

| Une caisse | Un personnage |
|---|---|
| Bascule sur ses arêtes | Reste toujours debout |
| Glisse sur une pente | Monte jusqu'à un certain angle, glisse au-delà |
| Bute sur une marche de 10 cm | La monte sans sauter |
| Rebondit contre un mur | S'arrête net, et glisse s'il le longe |

Chacun de ces comportements demanderait un correctif maison. C'est ainsi qu'on réécrit mal un contrôleur.

## 3.2 La décision

**Le contrôleur virtuel de Jolt.** Il n'est pas un corps de la simulation : il **interroge** le monde et résout lui-même ses déplacements. Il traite la montée de marche, le glissement le long des murs, l'angle de pente praticable et l'adhérence au sol en descente.

Les deux alternatives ont été écartées :
- *corps rigide à rotation bloquée* : très peu de code, mais les quatre défauts du tableau ci-dessus, chacun à corriger à la main ;
- *déplacement maison par balayage* : contrôle total du ressenti — ce qui compte dans un jeu d'horreur — mais des jours de mise au point sur les coins, les marches et les plafonds bas, pour égaler ce que Jolt fournit.

## 3.3 Trois réglages qui comptent

**La position désigne les pieds.** Une capsule est centrée sur son milieu ; la forme est donc décalée vers le haut, pour que la position du personnage soit ce qu'on pose sur un sol. Les yeux se placent ensuite à 1,65 m au-dessus.

**L'angle de pente maximal** est fixé à 46°. Au-delà, on glisse — c'est ce qui empêche de gravir un mur en le longeant, défaut classique des contrôleurs improvisés.

**Le plan de support** détermine quels contacts comptent comme du sol. Sans lui, un contact à mi-hauteur de la capsule — le coin d'une caisse, par exemple — ferait croire au personnage qu'il est posé, et il pourrait sauter en l'air.

## 3.4 La gravité n'est pas appliquée par le monde

Le contrôleur ne subit pas la gravité tout seul : c'est l'appelant qui compose sa vitesse à chaque pas. C'est voulu — un personnage doit rester **pilotable**, et la logique « au sol, la vitesse verticale repart de zéro » appartient au jeu, pas au moteur physique.

Ce détail évite un bug pénible : sans remise à zéro au sol, la gravité s'accumulerait pendant la marche, et le premier bord de marche provoquerait une chute à grande vitesse.

Le jeu lit la gravité depuis le monde plutôt que de recopier `-9,81` : une seule vérité pour une seule information.

## 3.5 Le sens de la chaîne

```
   entrees clavier  →  vitesse voulue  →  contrôleur physique  →  position finale  →  camera
```

La caméra ne décide plus de rien : elle **regarde** où la simulation a placé le joueur. Inverser ce sens — écrire la position de la caméra dans le contrôleur — redonnerait la traversée des murs.

## 3.6 Une erreur de test instructive

Le test du glissement échouait : le personnage se retrouvait à x = 5,1 alors que le mur est à x = 3. Le code n'était pas en cause — **le test l'était**. En poussant quatre secondes en diagonale, le personnage parcourait 12 m le long du mur, qui n'en fait que 20 de long depuis son centre : il avait simplement **contourné son extrémité**.

Un test qui vérifie la mauvaise chose est plus dangereux qu'un test absent : il donne une confiance injustifiée. Réduit à deux secondes, il mesure bien ce qu'il prétend.

## 3.7 Commandes

Z Q S D pour marcher, **Maj gauche** pour courir, **Espace** pour sauter, souris pour regarder. Le vol libre a disparu : le joueur est désormais soumis à la gravité.

## 3.8 Coût

Un balayage de capsule par pas, plus la résolution des contacts. Négligeable pour un personnage ; à surveiller le jour où l'IA en pilotera plusieurs.

## 3.9 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Trois tests sans GPU** : le personnage tombe et se pose les pieds exactement sur le sol, un mur l'arrête là où il doit, et il glisse le long d'un mur longé en diagonale au lieu de s'y coller. **34 tests, 93 assertions** au total.

**Ce qui n'existe pas encore** : ni accroupissement, ni hauteur de marche réglable, ni bruit de pas — les pas arriveront avec l'audio en M5. Le personnage ne pousse pas les caisses : un contrôleur virtuel n'applique pas de force aux corps qu'il touche, il faudra le faire explicitement.

**Ce qui vient après (brique 4)** : la saisie d'objets et les portes à la manière d'*Amnesia* — on tire sur la poignée, on ne joue pas une animation.
