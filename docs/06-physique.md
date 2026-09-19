# 06 — Physique (M4)

*Brique 1 : Jolt intégré, monde physique, corps statiques et dynamiques.*

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
