# 05 — Scène (M3)

*Brique 1 : entités et composants. Brique 2 : hiérarchie (section 2). Brique 3a : écrire une scène (section 3). Brique 3b : la relire (section 4). Brique 4 : le graphe de secteurs (section 5).*

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

---

# 3. Brique 3a — écrire une scène

## 3.1 Le problème

La scène était construite par `buildScene()`, en C++. Trois conséquences : aucun niveau ne peut exister sans recompiler, donc l'éditeur de M6 serait impossible ; rien n'est partageable ; et Git verrait du code là où il devrait voir du contenu.

## 3.2 Ce que le SPEC exige

> « Sérialisation déterministe dès le jour 1. IDs stables, clés triées, sortie identique pour une même scène. C'est le fix du problème de merge des assets binaires, impossible à rattraper plus tard. »

**Déterministe** veut dire : sauvegarder deux fois la même scène produit **exactement les mêmes octets**. Sans ça, chaque sauvegarde crée un faux changement, les diffs deviennent illisibles, et deux personnes travaillant sur des pièces différentes d'un même niveau entrent en conflit sur tout le fichier.

## 3.3 Les trois sources de non-déterminisme, et leur traitement

**L'ordre des entités.** EnTT ne garantit aucun ordre de parcours : il dépend de l'historique des créations et des destructions. Les entités sont donc **triées par identifiant** avant écriture.

**L'ordre des clés.** `nlohmann::ordered_json` conserve l'ordre d'insertion. Le JSON standard trierait par ordre alphabétique — déterministe aussi, mais `scale` apparaîtrait avant `position`, ce qui rend un fichier pénible à relire.

**L'écriture des flottants.** Un `float` converti en `double` s'écrit `0.34999999403953552`. Exact, mais illisible dans un diff. Les valeurs sont **arrondies au micromètre** : très en dessous de tout ce qui a un sens dans un jeu, et l'opération est **idempotente** — relire puis réécrire donne le même texte. Le zéro négatif est normalisé au passage, sans quoi `-0.0` apparaîtrait comme un faux changement.

## 3.4 Deux décisions de format

**Les identifiants sont écrits en hexadécimal, comme chaînes.** Un entier 64 bits dépasse la précision exacte des nombres JSON, que beaucoup d'outils lisent en `double`. Écrit comme nombre, un identifiant serait silencieusement modifié en passant par un formateur ou un éditeur.

**Le parent est désigné par l'identifiant du parent**, jamais par son rang dans le fichier. Le test le vérifie sur un cas piégeux : un enfant dont l'identifiant est plus petit que celui de son parent est écrit **avant** lui.

## 3.5 Les ressources : des poignées, pas des pointeurs

Un `MeshRenderer` contenait des pointeurs. Une adresse mémoire change à chaque lancement : elle ne peut pas être écrite dans un fichier.

Le composant porte désormais des **poignées** vers une `ResourceTable`, qui fait la correspondance entre un **nom logique** (`suzanne`, `damier`) et la ressource chargée. Conséquences : renommer un fichier sur le disque ne casse aucune scène, et l'éditeur de M6 pourra lister ce qui est disponible en parcourant la table.

## 3.6 Les identifiants : aléatoires plutôt qu'incrémentés

Chaque entité reçoit un nombre tiré au hasard sur 64 bits. Avec un compteur, deux personnes créant des objets chacune de leur côté utiliseraient toutes deux 1, 2, 3 — fusionner leurs pièces demanderait de tout renuméroter, et **toute référence entre entités casserait**. Avec des identifiants aléatoires, la collision est négligeable : il faudrait environ 5 milliards d'entités pour atteindre une chance sur un milliard.

## 3.7 La vérification la plus parlante

Deux sauvegardes successives, en jeu, pendant que la statue tourne. Résultat du diff :

```
68c68
<           0.344166,
---
>           0.857499,
```

**Une seule valeur diffère** : la rotation de la statue, qui a effectivement tourné de 0,51 radian entre les deux sauvegardes — cohérent avec 0,35 rad/s pendant une seconde et demie. Tout le reste est identique octet pour octet.

C'est précisément ce que le SPEC cherche : dans un diff Git, **seul ce qui a vraiment changé apparaît**.

Cinq tests unitaires couvrent le reste, et tournent en CI puisqu'ils ne demandent aucun GPU : même scène deux fois, ordres de création inversés, référence de parent, arrondi des flottants, présence du numéro de version.

## 3.8 Le numéro de version

Le fichier porte `"version": 1`. Un fichier écrit par une version ultérieure sera **refusé** plutôt que mal interprété : mieux vaut un message clair qu'un niveau silencieusement cassé. La migration viendra quand il y aura quelque chose à migrer.

## 3.9 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié** : F5 écrit `assets/scenes/demo.json`, lisible, déterministe, avec noms de ressources et liens de parenté.

**Ce qui n'existe pas encore** : rien ne relit ce fichier. La scène est toujours construite par du code au démarrage.

**Ce qui vient après (brique 3b)** : la lecture. Modifier le JSON à la main et voir la scène changer au lancement — le moment où les niveaux cessent d'être du C++.

---

# 4. Brique 3b — relire une scène

## 4.1 Le problème, et une conséquence qu'on n'avait pas vue venir

Écrire ne sert à rien sans lire. Mais un détail est apparu en cours de route, et il éclaire tout le reste.

Tant que la scène est **construite par du code**, chaque lancement crée de nouvelles entités, donc tire de **nouveaux identifiants aléatoires**. Sauvegarder produit alors un fichier entièrement différent du précédent — 119 lignes de diff à chaque fois, alors que rien n'a changé.

La lecture règle ça : les identifiants viennent du fichier et ne bougent plus. C'est seulement à partir de maintenant que la promesse « diff minimal » du SPEC tient réellement.

## 4.2 Les trois décisions

**Tout ou rien.** La scène se construit dans un objet temporaire, et ne remplace celle de l'appelant qu'en cas de succès complet. Un niveau à moitié chargé — la moitié des murs, aucune lumière — se diagnostique bien plus difficilement qu'un échec net. Un test le vérifie : après une lecture échouée, la scène précédente est intacte.

**Deux passes.** On crée d'abord **toutes** les entités avec leurs identifiants, puis on attache les composants et les liens de parenté. Un enfant peut parfaitement être écrit avant son parent — le fichier est trié par identifiant, pas par hiérarchie. Un test couvre ce cas précis.

**Ressource introuvable : un défaut visible.** Un nom inconnu fait chercher une ressource nommée `missing`, un magenta franc impossible à confondre avec une texture légitime. C'est la pratique du métier, et elle repose sur une idée simple : un objet silencieusement absent se diagnostique bien plus mal qu'un objet visiblement faux.

## 4.3 Les exceptions, traitées exactement comme le SPEC le prévoit

`nlohmann::json` lève une exception sur un document malformé, et le SPEC interdit les exceptions dans le moteur. La règle 6 prévoyait précisément ce cas :

> « On ne les interdit pas dans ces libs, mais on catch systématiquement à la frontière du wrapper et on convertit en code d'erreur avant de remonter dans le moteur. »

En pratique, `Json::parse(json, nullptr, false)` demande à la bibliothèque de **ne pas lever** et de rendre un document marqué comme rejeté. L'erreur est convertie en `false` dès la première ligne du chargeur, et rien ne remonte. Un test passe une chaîne délibérément malformée pour le vérifier.

## 4.4 Le numéro de version sert enfin

Un fichier portant une version inconnue est **refusé**, pas interprété au mieux. Un niveau silencieusement cassé coûte bien plus cher qu'un message clair. La migration viendra quand il y aura quelque chose à migrer.

## 4.5 La vérification décisive

Le fichier `demo.json` a été modifié **à la main** — la position de la statue passée de `[0, 0, 0]` à `[-2.2, 0.9, 0]` — puis le jeu relancé **sans recompiler**.

La statue apparaît déplacée, et son satellite l'a suivie, puisque la hiérarchie vient elle aussi du fichier.

Sept tests couvrent le reste, tous sans GPU donc exécutés en CI :

| Test | Ce qu'il empêche |
|---|---|
| Aller-retour neutre | Ouvrir un niveau et le refermer sans rien toucher produirait un diff |
| Enfant avant parent | Un format qui dépendrait de l'ordre d'écriture |
| Version inconnue refusée | Un niveau interprété de travers par une version antérieure |
| JSON malformé refusé | Une exception qui traverserait le moteur |
| Scène intacte après échec | Un niveau à moitié chargé |

## 4.6 Coût

Deux parcours du fichier et une recherche linéaire d'identifiant par lien de parenté. Sur des centaines d'entités, c'est négligeable ; sur des dizaines de milliers, il faudra une table de hachage. Le jour où ça se mesurera.

## 4.7 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié** : la scène du jeu vient entièrement du fichier. `buildScene()` a disparu du code.

**Ce qui n'existe pas encore** : aucun rechargement à chaud — il faut relancer. Rien ne valide qu'un fichier édité à la main reste cohérent (une échelle nulle, un cône de spot négatif passeront sans broncher). Et le nom d'entité n'est pas unique : `findByName` rend la première trouvée, ce qui convient au code de démonstration mais pas à une référence durable.

**Ce qui vient après (brique 4)** : le graphe de secteurs et portails — la dernière brique de M3, et la fondation que réutiliseront le culling du rendu, l'occlusion audio de M5 et la propagation du son pour l'IA de M7.

---

# 5. Brique 4 — le graphe de secteurs et portails

## 5.1 Le problème

Trois systèmes, dans trois jalons différents, ont besoin de la même information : **quelles parties du niveau sont reliées entre elles, et par où**.

- Le **rendu** ne doit dessiner que ce qui est atteignable depuis la pièce du joueur. Un couloir derrière trois murs ne se voit pas, quel que soit l'angle de la caméra.
- L'**audio** (M5) doit faire passer le son par les ouvertures. Une porte fermée atténue et filtre ; un mur bloque.
- L'**IA** (M7) doit entendre par le même chemin : un bruit derrière un mur porte moins loin qu'à vol d'oiseau.

Le SPEC place cette structure en M3 précisément parce qu'elle est partagée : la poser trois fois, une par système, garantirait trois comportements incohérents.

## 5.2 Les décisions

**Des boîtes alignées sur les axes.** Un secteur est un pavé droit décrit par un centre — celui de son `Transform` — et des demi-dimensions. Le test d'appartenance tient en six comparaisons, sans racine carrée ni produit scalaire. Les volumes convexes quelconques seraient plus généraux, mais coûteraient un test plus cher, une saisie pénible à la main, et un outil de construction à écrire **avant** l'éditeur.

Limite assumée : une pièce en L demande deux secteurs qui se recouvrent. C'est très bien ainsi — le découpage est posé à la main, comme le prévoit le SPEC.

**Des entités, comme tout le reste.** Un secteur et un portail sont des entités porteuses d'un composant. Conséquence : ils sont **sérialisés, hiérarchisables et éditables gratuitement**, sans une ligne de code spécifique. Un test le vérifie sur un cas parlant — un secteur attaché à un ascenseur monte avec lui.

**Le graphe seul dans cette brique.** Le culling du rendu viendra dans une PR séparée, avec des mesures Tracy à l'appui. Mélanger une structure de données et une optimisation de rendu dans la même PR rendrait les deux plus difficiles à juger.

## 5.3 Vocabulaire

- **Secteur** : un volume du niveau — une pièce, un couloir.
- **Portail** : l'ouverture entre deux secteurs. Une porte, une arche, un trou dans un mur.
- **Parcours en largeur** : on explore les voisins immédiats avant les suivants. Les secteurs sortent donc **triés par nombre de portails traversés**, ce dont l'audio se servira pour atténuer par étapes.
- **Profondeur** : le nombre de portails qu'on autorise à franchir.

## 5.4 Les deux requêtes

`sectorAt(scène, position)` — dans quel secteur se trouve ce point. Elle lit la **matrice monde**, pas le `Transform` local : un secteur enfant d'un ascenseur suit l'ascenseur.

`reachableSectors(scène, depuis, profondeur, sortie)` — quels secteurs sont atteignables en franchissant au plus *n* portails. Le parcours mémorise les secteurs déjà atteints : un niveau bouclé — trois pièces reliées en cercle — ne provoque aucune boucle infinie. Un test couvre ce cas.

## 5.5 Une leçon rencontrée pendant les essais

En pilotant le jeu pour franchir la limite entre deux secteurs, la touche envoyée ne produisait aucun mouvement. Cause : le script envoyait la **touche virtuelle Windows `A`**, que le système place sur la **position physique Q** d'un clavier AZERTY. Or le moteur lit les **positions physiques**, par choix délibéré documenté en M1.

L'illustration est parfaite : le même choix qui donne ZQSD sans configuration rend les outils d'automatisation dépendants de la disposition. Une fois la bonne touche envoyée, les transitions apparaissent :

```
secteur : aile_est (2 atteignables a 1 portail)
secteur : aile_ouest (2 atteignables a 1 portail)
secteur : aile_est (2 atteignables a 1 portail)
```

## 5.6 Coût

Le test d'appartenance parcourt les secteurs linéairement. Sur quelques dizaines, c'est négligeable ; sur un millier, il faudra un index spatial. Le parcours en largeur est borné par la profondeur demandée, et réutilise un vecteur fourni par l'appelant — aucune allocation en régime établi.

## 5.7 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié** : le jeu annonce son secteur courant et le nombre de secteurs atteignables, transitions comprises. Cinq tests couvrent l'appartenance, la hiérarchie, les profondeurs, les cycles et le cas dégénéré.

**Ce qui n'existe pas encore** : rien ne consomme le graphe. Les portails ont des dimensions mais ne servent pas encore à tester la visibilité — seulement l'adjacence. Aucun outil ne vérifie qu'un découpage posé à la main est cohérent (un secteur oublié laisse un trou, deux secteurs qui se recouvrent sont acceptés sans avertissement).

**Ce qui vient après** : le **culling par portails** dans le renderer, avec des mesures Tracy — et ce sera la première fois que le graphe rendra quelque chose de mesurable.

---

# 6. Bilan de M3

Le jalon est terminé. Le moteur est passé des variables membres aux données.

| Brique | État |
|---|---|
| Entités et composants (EnTT) | ✅ |
| Hiérarchie de transforms | ✅ |
| Écriture déterministe en JSON | ✅ |
| Lecture, tout ou rien, versionnée | ✅ |
| Graphe de secteurs et portails | ✅ |

**Ce qui a changé en pratique** : un niveau est désormais un fichier. Le modifier ne demande plus de recompiler, et un diff Git ne montre que ce qui a réellement bougé. C'est la condition d'existence de l'éditeur de M6.

**Les dettes assumées :**

- pas de rechargement à chaud d'une scène ;
- aucune validation des données éditées à la main — une échelle nulle ou un cône négatif passent sans broncher ;
- `findByName` rend la première entité trouvée : commode, mais insuffisant comme référence durable ;
- détruire un parent laisse ses enfants orphelins, traités comme des racines ;
- recherche linéaire dans le graphe et dans la table de ressources, à remplacer le jour où la mesure le réclamera.

**Ce qui vient après (M4 — Physique)** : Jolt, colliders depuis glTF, contrôleur de personnage, saisie d'objets et portes. Le moment où la caméra cessera de traverser les murs.
