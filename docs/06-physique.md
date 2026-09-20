# 06 — Physique (M4)

*Brique 1 : Jolt intégré, monde physique. Brique 2 : les colliders deviennent des données (section 2). Brique 3 : le contrôleur de personnage (section 3). Brique 4a : attraper et pousser (section 4). Brique 4b : les portes à charnière (section 5). Brique 5 : la collision de maillage (section 6).*

Le jalon M4 apporte la physique : colliders, contrôleur de personnage, saisie d'objets et portes. Cette première brique pose le socle — et force au passage une décision restée en suspens depuis M3.

**M4 est terminé.**

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

---

# 4. Brique 4a — attraper et pousser

## 4.1 Le problème

Deux manques, dont l'un relevé en jouant : **les caisses arrêtaient le joueur comme des murs**. Un contrôleur virtuel détecte les corps dynamiques mais ne leur transmet aucune force — il faut le lui apprendre.

Et surtout, rien ne permettait de **manipuler** quoi que ce soit, alors que c'est le cœur du genre que vise le SPEC.

## 4.2 La décision : une vitesse imposée, pas une liaison

Trois façons de faire suivre un objet tenu :

**Vitesse imposée vers la cible** — à chaque pas, on calcule la vitesse qui rapprocherait l'objet du point de maintien, et on la lui donne. L'objet reste un corps dynamique ordinaire.

**Contrainte physique** — une liaison entre l'objet et un point invisible, résolue par le solveur. Plus juste physiquement, mais une liaison mal dosée devient instable, et le réglage dépend de la masse : chaque objet demanderait son propre accord.

**Objet rendu cinématique** — on désactive sa physique et on le colle devant la caméra. Parfaitement stable, et catastrophique : il traverse les murs, et on peut passer une caisse à travers une porte fermée. Exactement ce que le SPEC cherche à éviter.

**Choix : la vitesse imposée.** Un seul réglage de nervosité, stable quelle que soit la masse, et l'objet conserve toutes ses collisions. C'est l'approche d'*Amnesia* et de *Half-Life 2*.

## 4.3 Trois garde-fous

**La vitesse est plafonnée.** Sans plafond, un objet très éloigné recevrait une vitesse énorme et franchirait un mur en un seul pas de simulation — le tunnel classique.

**L'objet est lâché s'il s'éloigne trop.** Il s'est coincé dans un mur ou derrière une porte : le ramener de force reviendrait à le faire passer au travers.

**Un corps endormi doit être réveillé.** Jolt cesse de simuler les objets immobiles ; leur imposer une vitesse sans les réveiller ne fait rien du tout. C'est un piège silencieux — la caisse reste figée malgré la poussée — et un test le couvre explicitement.

**L'objet tenu ne heurte plus son porteur.** Signalé en jouant : ramener une caisse contre soi **propulsait le joueur**. Logique — c'est un corps dynamique piloté à vitesse imposée qui entre en contact avec la capsule, et Jolt fait exactement ce qu'on lui demande.

La correction est une **couche de collision dédiée**. Le personnage a désormais la sienne, et un objet tenu passe dans une couche qui ne la rencontre pas — tout en continuant de heurter les murs et les autres objets. Changer de couche suffit : l'objet garde sa masse, sa forme et sa vitesse, il cesse simplement d'exister pour le porteur.

C'est ce que font tous les jeux du genre, et un test de non-régression le couvre : on pousse délibérément une caisse tenue dans le joueur et on vérifie qu'il ne bouge pas.

## 4.4 Le lancer de rayon, qui resservira

Attraper commence par viser : un rayon part de l'œil, dans l'axe du regard, sur la portée du bras. Le premier corps touché est le candidat, à condition qu'il soit dynamique.

Cette même fonction servira au **champ de vision de l'antagoniste** en M7 : voir, c'est lancer un rayon et regarder ce qu'il rencontre. Un test vérifie déjà la propriété qui rendra le monstre aveugle derrière un mur — **le rayon s'arrête au premier corps**.

## 4.5 Pousser ce qu'on bouscule

Un rayon court, à hauteur de hanche, dans la direction de marche. S'il touche un corps dynamique, on lui ajoute une vitesse. C'est volontairement simple : doser par la masse, tenir compte de l'angle d'impact et de la friction viendra si le ressenti le réclame.

## 4.6 Commandes

**Clic gauche maintenu** pour attraper, relâché pour lâcher. L'objet flotte à 1,40 m devant les yeux et suit le regard — en restant physique : il heurte les murs, et il repousse les autres caisses.

Le premier essai utilisait la touche **E** en bascule : un appui pour prendre, un autre pour lâcher. Le geste juste est le maintien, et il ne s'agit pas d'un détail de confort. Une bascule dit « cet objet est à moi jusqu'à nouvel ordre » ; un maintien dit « je tire dessus **en ce moment** ». Pour une porte qu'on entrouvre de dix centimètres pour regarder derrière, c'est tout le sujet du genre. C'est le geste d'*Amnesia*, et le SPEC le demandait déjà.

## 4.7 Coût

Un lancer de rayon par frame pour la saisie, un autre pendant la marche. Un rayon coûte une descente dans l'arbre spatial : quelques microsecondes.

## 4.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : la caisse attrapée flotte devant les yeux, suit le regard, et reste tenue quand le joueur recule.

**Cinq tests** s'ajoutent : le rayon trouve le corps visé à la bonne distance avec la bonne normale, une portée trop courte ne touche rien, le rayon s'arrête au plus proche, les corps statiques ne sont pas saisissables, et un corps endormi se réveille bien quand on lui impose une vitesse. **39 tests, 109 assertions** au total.

**Ce qui n'existe pas encore** : l'objet tenu ne tourne pas avec le regard, on ne peut pas le lancer, et aucun retour visuel n'indique ce qu'on vise. La poussée ignore la masse.

**Ce qui vient après (brique 4b)** : les portes à charnière — le dernier morceau de M4, et celui que le SPEC décrit avec le plus de précision.


---

# 5. Brique 4b — les portes à charnière

## 5.1 Le problème

Une porte n'est pas une caisse. Une caisse est libre : six degrés de liberté, elle va où on la pousse. Une porte n'en a **qu'un seul** — une rotation autour de ses gonds, bornée par le chambranle d'un côté et par le mur de l'autre.

Rien de ce qui a été construit en brique 4a ne sait exprimer ça. Imposer une vitesse à une porte, comme on le fait pour une caisse, reviendrait à lui demander de quitter ses gonds.

## 5.2 La décision : une contrainte, pas un scénario

Deux façons de faire une porte :

**L'animer.** Un état ouvert, un état fermé, une interpolation entre les deux. C'est ce que font la plupart des jeux, et c'est bien moins cher. Mais la porte devient un décor scripté : elle ne peut pas être entrouverte de douze centimètres, elle ne résiste pas quand une caisse la bloque, et un monstre ne peut pas la pousser pendant que le joueur la retient.

**La contraindre.** On déclare à la physique que ce corps ne peut tourner qu'autour d'un axe donné, entre deux butées, et on le laisse vivre. C'est le choix retenu, parce que la porte manipulée à la main est un **élément de gameplay** dans ce genre, pas un habillage.

Jolt appelle ça une `HingeConstraint`. Elle prend un point d'ancrage, un axe, deux angles limites et un couple de frottement. Le second corps de la contrainte est `Body::sFixedToWorld` : la porte est accrochée au monde lui-même.

## 5.3 Le couple, ou pourquoi on tire sur une poignée

Pour faire pivoter la porte, l'API physique gagne `applyImpulseAtPoint`. La distinction avec une impulsion ordinaire est la clé de toute la brique :

- Une impulsion appliquée **au centre de masse** ne produit qu'une translation. Sur une porte à charnière, la contrainte l'annule entièrement — la porte ne bouge pas.
- La même impulsion appliquée **à distance du centre** produit un couple, proportionnel au bras de levier. C'est ce qui la fait tourner.

C'est exactement pourquoi les poignées de porte sont à l'opposé des gonds. Le moteur reproduit la physique réelle parce qu'il la simule vraiment, et non parce qu'on l'a programmée à ressembler à une porte.

Le jeu s'en sert ainsi : au clic, le rayon renvoie le **point** touché. On mémorise son décalage par rapport au centre du corps, et chaque frame on applique une impulsion vers l'endroit que vise le regard, **au point saisi**. Tirer sur le bord libre ouvre grand ; pousser près des gonds ne fait presque rien.

Le point saisi est recalculé à chaque frame depuis la position du corps, sinon on continuerait de tirer sur un endroit que la porte a quitté.

## 5.4 Ce qui distingue une porte d'une caisse, côté code

Une seule question : `isBodyHinged()`. Le monde physique tient la liste des corps contraints, et le jeu la consulte pour choisir sa manière de manipuler l'objet visé — impulsion au point pour une porte, vitesse imposée pour une caisse.

Une conséquence moins évidente : une porte tenue **reste solide pour le joueur**. Une caisse tenue passe dans une couche de collision qui ignore son porteur (voir 4.3), sans quoi elle le propulse. Une porte, non : traverser une porte qu'on est en train d'ouvrir n'aurait aucun sens.

## 5.5 Une porte, ça pesait 144 kg

Les premiers tests de charnière ont tous échoué, et la leçon vaut d'être écrite.

Une impulsion de 1,5 N·s sur le bord du battant ne le faisait pas bouger d'un centimètre. Le calcul explique tout : un panneau de 0,9 × 2 × 0,08 m avec la masse volumique par défaut de Jolt — **1000 kg/m³, celle de l'eau**, donc un solide plein — pèse **144 kg**, et son moment d'inertie autour des gonds avoisine **39 kg·m²**.

La première réaction a été de monter les impulsions dans les tests jusqu'à ce qu'ils passent. C'était traiter le symptôme : les tests décrivaient alors une porte en béton, et **en jeu la traction à la main restait sans effet** — clic maintenu, aucun mouvement visible.

Le vrai correctif est une **masse volumique par corps**, remontée jusqu'au fichier de scène. Jolt la déduit de la forme : `BoxShapeSettings::SetDensity()`, et masse comme inertie en découlent. Une porte en bois creux est à 150 kg/m³, soit **21 kg** pour ce battant, et **5,8 kg·m²** d'inertie — quelque chose qu'un bras humain peut ouvrir.

Deux réglages ont suivi la nouvelle échelle : le frottement des gonds, ramené de 40 à **6 N·m**, et la traction du joueur, montée à 25 avec un plafond — sans quoi viser loin sur le côté enverrait la porte claquer contre sa butée.

Un dernier détail, instructif : le calcul annonçait un arrêt en une seconde, la mesure en donne **deux**. Le frottement du solveur de Jolt n'est pas exactement un couple constant. Le test a été réécrit autour de la **valeur mesurée**, avec deux vérifications qui, elles, ne dépendent d'aucun réglage fin : la porte a réellement tourné, et elle s'est arrêtée **avant** ses butées — sinon c'est le chambranle qu'on testerait, pas le frottement.

Ce qu'il faut retenir : **en physique, un test qui échoue accuse souvent les ordres de grandeur, pas le code.** Le réflexe utile est de calculer la masse et l'inertie avant de toucher au moteur — et de se méfier d'un réglage qu'on augmente jusqu'à ce que le test passe.

## 5.6 La charnière est une donnée de la scène

Comme les colliders en brique 2, la charnière est un composant sérialisé — ancrage, axe, butées, frottement :

```json
"collider": { "shape": "box", "halfExtents": [0.45, 1.0, 0.04],
              "static": false, "density": 150.0 },
"hinge": { "anchor": [-0.45, 0, 0], "axis": [0, 1, 0],
           "minAngle": -1.6, "maxAngle": 0.0, "friction": 6.0 }
```

La masse volumique n'est écrite que pour les corps dynamiques : un mur a une masse infinie par définition, et l'indiquer ne décrirait rien.

L'ancrage est exprimé **dans le repère de l'entité** — un demi-battant sur le côté, là où se trouveraient les gonds — puis converti en coordonnées du monde à la création du corps. Sans cette conversion, toutes les portes du niveau pivoteraient autour de l'origine de la scène.

Les butées asymétriques `[-1,6 ; 0]` décrivent une porte qui ne s'ouvre que **dans un sens**, comme celle d'une pièce dont le mur bloque l'autre côté.

## 5.7 Le réglage : la main est un ressort amorti

Signalé en jouant : *« la porte a une physique trop légère, elle va super vite quand je la bouge »*. Trois défauts distincts se cachaient derrière ce ressenti, et le plus grave n'est pas la masse.

**La force était appliquée par frame, pas par pas de simulation.** Une force qui s'exerce dans `onFrame` dépend de la vitesse de la machine : sur un écran à 144 Hz, la porte recevait deux fois et demie plus d'impulsions que sur un 60 Hz. C'est précisément le défaut que la boucle à pas fixe de M0 existe pour empêcher — et l'oubli était structurel, pas cosmétique. La traction vit maintenant dans `onFixedUpdate`, et l'impulsion vaut *force × pas*, donc le même résultat quelle que soit la machine.

**Il n'y avait aucun terme de vitesse.** Une force proportionnelle à l'écart, répétée à chaque pas, **accélère indéfiniment** : rien ne s'oppose à la vitesse acquise. La main réelle ne fonctionne pas ainsi — elle tire d'autant plus fort que la porte est loin de là où on la veut, et elle **freine** d'autant plus que la porte va vite. C'est un ressort amorti :

```
force = (cible − point_saisi) × raideur  −  vitesse_du_point × amortissement
        └──────── on tire ────────┘         └──── on freine ────┘
```

La vitesse du point saisi n'est pas celle du centre : un corps en rotation n'a pas de vitesse unique, elle vaut **ω × r** et croît avec la distance à l'axe. C'est ce qui a demandé un `bodyAngularVelocity()` dans l'API physique.

Les deux coefficients (400 N/m, 340 N·s/m) sont pris proches de l'**amortissement critique** pour la masse effective au point saisi — environ 73 kg, soit *I/r²*. La porte rejoint la main sans osciller ni dépasser.

**Et la masse était trop faible.** Corriger 144 kg en 21 kg était un excès inverse : 21 kg, c'est une porte creuse de placard. À 300 kg/m³ — du bois plein — le battant pèse **43 kg**, et le frottement des gonds suit à 15 N·m.

La leçon prolonge celle de 5.5 : **un mauvais ordre de grandeur se corrige par calcul, pas par tâtonnement** — et quand un objet « va trop vite », il faut d'abord chercher le terme d'amortissement manquant avant d'alourdir l'objet.

## 5.8 Coût

Une contrainte résolue par itération du solveur, pour quelques dizaines de portes dans un niveau : négligeable. `isBodyHinged` est une recherche linéaire dans un petit vecteur — à revoir si un niveau comptait des centaines de corps contraints, pas avant.

## 5.9 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : la porte de la scène de démonstration s'ouvre en tirant sur son bord libre, s'arrête sur sa butée, et reste solide.

**Six tests** s'ajoutent : tirer sur le bord libre fait pivoter le battant, la charnière le retient malgré une impulsion violente, les butées l'arrêtent, le frottement finit par l'immobiliser sans l'aide des butées, un corps contraint est reconnu comme tel, et une caisse libre ne l'est pas. **45 tests, 128 assertions** au total.

**Ce qui n'existe pas encore** : pas de tiroirs (une glissière, donc une contrainte différente), pas de porte verrouillée ni de clé, pas de poignée qu'on abaisse, et aucun son au contact — c'est M5 qui l'apportera. La masse volumique est réglée objet par objet, alors qu'elle devrait découler d'un **matériau** — la même donnée servira au son des pas et à l'occlusion audio.

**M4 est terminé.** La suite, c'est l'audio — le système central de ce moteur.

---

# 6. Brique 5 — la collision de maillage

## 6.1 Le problème

La collision du décor était décrite par des **boîtes posées à la main**. Six pour une pièce cubique : le sol, le plafond, et quatre murs. C'était acceptable pour une pièce de démonstration, et c'est impraticable pour un vrai niveau.

Un couloir en L, un escalier, un mur oblique, une voûte : rien de tout cela ne s'approche correctement par des boîtes alignées sur les axes. Et surtout, **on ne peut pas importer un niveau** — un modèle téléchargé ou exporté de Blender arrive avec sa géométrie, pas avec une liste de boîtes. Sans collision de maillage, on tomberait au travers à l'infini.

## 6.2 La décision : un arbre de triangles

Jolt sait construire une forme de collision à partir d'une **liste de triangles** (`MeshShape`). Il en fait un arbre spatial, ce qui rend le test de collision logarithmique et non proportionnel au nombre de faces — un décor de cent mille triangles coûte à peine plus qu'un décor de mille.

Deux contraintes viennent avec, et elles ne sont pas négociables.

**Obligatoirement statique.** Un maillage de triangles n'a ni volume ni masse bien définis : il est creux, potentiellement ouvert, et rien ne dit où se trouve son centre de gravité. Aucun moteur physique ne sait faire rouler ça. Ce n'est pas une limitation gênante — le décor ne bouge pas, c'est exactement ce qu'on lui demande. Le chargeur force donc `static` à vrai, quoi que dise le fichier.

**À face unique.** Un triangle a un devant et un derrière, décidés par l'**ordre de ses indices** : le produit vectoriel des deux premières arêtes donne la normale. Une face enroulée à l'envers est traversée sans rien heurter.

C'est le piège de cette brique, et il s'est présenté immédiatement : le premier test faisait tomber une caisse **au travers** du sol, et un rayon venu d'en haut rapportait une normale pointant vers le bas. La géométrie du test était enroulée à l'envers — le moteur, lui, était correct. Un défaut de ce genre ne produit aucune erreur : juste un décor traversable, qu'on ne découvre qu'en y marchant.

Pour la pièce, cela tombe juste : son maillage est **tourné vers l'intérieur** — c'est ce qui permettait déjà de la voir de l'intérieur sans que les murs disparaissent. Les faces regardent donc le joueur, et la collision fonctionne du bon côté.

## 6.3 Collision et affichage restent séparés

La géométrie de collision est une ressource **distincte** du maillage d'affichage, même quand les deux coïncident. La raison est celle de M4 : la collision d'un décor est presque toujours plus grossière que sa géométrie visible. Un mur sculpté de mille triangles se heurte très bien avec deux.

Une conséquence technique force d'ailleurs la séparation : **un maillage GPU ne se relit pas**. Une fois les sommets envoyés à la carte, ils ne sont plus accessibles au processeur. Le jeu garde donc une copie — positions et indices seulement, ni normales ni UV, qui ne servent pas à la collision.

La table de ressources n'en garde qu'une **vue** : elle ne possède rien, comme pour les maillages GPU. Les tableaux doivent lui survivre, et c'est au jeu de s'en assurer.

## 6.4 L'échelle enveloppe la forme

Jolt applique l'échelle par une **forme enveloppante** (`ScaledShape`) plutôt qu'en déformant les sommets. Deux objets de tailles différentes partagent ainsi le même arbre de triangles : une seule construction, une seule empreinte mémoire.

Une échelle **négative** est refusée. Elle retournerait les triangles, donc les faces regarderaient vers l'extérieur, et le décor deviendrait traversable — encore un défaut qui ne se verrait qu'en jouant.

## 6.5 Ce que la scène y gagne

Sept entités de collision disparaissent, remplacées par une ligne sur la pièce elle-même :

```json
"collider": { "shape": "mesh", "collisionMesh": "piece", "static": true }
```

La collision suit désormais **exactement** les murs, y compris le plafond, et elle suivra n'importe quelle forme — c'est ce qui rend l'import d'un niveau possible.

La démonstration des deux matières de pas, qui reposait sur deux demi-sols, se fait maintenant par une **estrade en bois** posée sur le sol de pierre. C'est un meilleur exemple : on l'escalade, donc on entend le changement sous ses pieds au lieu de traverser une frontière invisible.

## 6.6 Une limite à connaître

La matière d'une surface est portée par l'**entité**, pas par le triangle. Un décor entier en un seul collider n'a donc qu'un seul son de pas.

Jolt sait associer un matériau à chaque triangle, et c'est la vraie réponse — elle viendra quand un niveau réel en aura besoin. En attendant, les surfaces qui doivent sonner différemment restent des entités séparées, ce qui est de toute façon souvent ce qu'on veut : une estrade, une plaque de métal, une flaque.

## 6.7 Coût

Un arbre construit une fois au chargement, proportionnel au nombre de triangles. Le test de collision est logarithmique. La copie processeur de la géométrie coûte douze octets par sommet plus quatre par indice — pour la pièce de démonstration, quelques kilo-octets.

## 6.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Cinq tests** s'ajoutent : une caisse atterrit sur un sol de maillage au lieu de le traverser, un rayon le touche à la bonne distance avec la bonne normale et ne touche rien au-delà de ses bords, une mise à l'échelle étend réellement sa surface, une géométrie invalide est refusée sans faire tomber le monde, et un corps de maillage est statique donc ne tombe jamais. Le garde-fou de la scène vérifie en plus que chaque collider de maillage cite une géométrie que le jeu enregistre. **91 tests, 594 assertions** au total.

**Ce qui n'existe pas encore** : pas de matériau par triangle, pas de génération automatique d'une collision simplifiée à partir d'un maillage détaillé, et pas de forme convexe pour les objets dynamiques — une caisse reste une boîte.

**Ce qui vient après** : un vrai niveau. Le moteur sait désormais charger une géométrie, ses matériaux et sa collision ; il ne lui manque plus qu'un niveau à charger.
