# 09 — L'éditeur (M6)

*Brique 1 : l'éditeur existe — hiérarchie, inspecteur, bascule (section 1). Brique 2 : les gizmos (section 2). Brique 3 : créer, dupliquer, détruire, enregistrer (section 3). Brique 4 : l'inspecteur complet (section 4). Brique 5 : sélectionner en cliquant (section 5). Brique 6 : deux modes, pas deux couches (section 6).*

# 1. Brique 1 — l'éditeur existe

## 1.1 Le problème

Modifier le niveau voulait dire **éditer `demo.json` à la main et relancer**. Acceptable pour une pièce de démonstration ; intenable dès qu'il faut placer cinquante objets.

Le coût s'est d'ailleurs fait sentir juste avant ce jalon : poser correctement une poignée de 20 cm sur une porte a demandé **quatre allers-retours**, chacun coûtant un lancement du jeu. Calculer une rotation et une compensation d'échelle sur le papier, pour un objet qu'on pourrait attraper et tourner à la souris, est exactement le travail que cet éditeur supprime.

Et c'est l'objectif du projet : un moteur où **un non-développeur** crée son jeu.

## 1.2 Les options

**Un éditeur séparé, en deux processus**, comme Unity et Unreal. C'est le modèle de l'industrie, et il impose une **sérialisation permanente** entre les deux programmes, plus un mode « play » qui relance tout. Beaucoup de machinerie pour un moteur mono-fenêtre destiné à un seul genre.

**Un éditeur intégré, affiché par-dessus le jeu.** C'est le choix retenu, et son avantage est structurel plutôt que pratique : **il n'y a jamais de séparation à franchir**. Le jeu tourne déjà, l'éditeur modifie ses données en place. Le *play-in-editor* que le SPEC réclame n'est pas une fonctionnalité à écrire — c'est l'état par défaut.

## 1.3 Le mode immédiat, et pourquoi il convient ici

Dear ImGui, verrouillé par le SPEC, travaille en **mode immédiat** : on ne construit pas un arbre de widgets qu'on maintient, on **redessine tout à chaque image**.

Pour une application ordinaire, c'est du gaspillage. Pour un éditeur, c'est exactement le bon modèle : les données changent en permanence — la physique bouge les objets, la statue tourne — et il n'existe **aucun état d'interface à synchroniser avec la scène**. Donc aucun moyen que les deux divergent.

C'est la même logique que la passe de rendu : on relit la scène à chaque image plutôt que de propager des notifications de changement.

## 1.4 Faire entrer SDL dans l'éditeur sans le sortir de la plateforme

ImGui a besoin de la **vraie fenêtre SDL** et des **événements bruts**. Or la règle 2 du SPEC interdit qu'un type SDL apparaisse dans un en-tête public.

Deux ajouts minimaux règlent ça, et tous deux passent par des **pointeurs opaques** :

- `Window::nativeWindow()` et `nativeGlContext()` rendent des `void*`. Aucun type SDL n'apparaît, et personne ne peut s'en servir par accident : il faut savoir ce qu'on caste pour en faire quoi que ce soit.
- `Input::setRawEventObserver()` prend un pointeur de fonction appelé pour **chaque** événement, avant que le moteur ne le traite.

Pourquoi un observateur plutôt que d'enrichir `InputState` ? Parce que notre état d'entrée ne retient qu'une poignée de touches, alors qu'une interface a besoin du clavier complet, du texte saisi et de la molette. Gonfler l'état du moteur avec tout ce dont une interface *pourrait* avoir besoin serait le faire payer à tout le monde ; laisser l'éditeur écouter la source ne coûte rien à personne.

Un **pointeur de fonction** et non une `std::function` : aucune allocation, et l'appel reste direct dans une boucle qui tourne des milliers de fois par seconde.

## 1.5 Qui a la souris

Une interface se clique, un jeu à la première personne **capture** la souris. Les deux ne peuvent pas coexister, et la bascule `F1` tranche : l'éditeur apparaît, le curseur est rendu.

Mais ça ne suffit pas. Même l'éditeur ouvert, ImGui ne veut la souris que lorsqu'elle survole une de ses fenêtres. Il expose pour ça `WantCaptureMouse` et `WantCaptureKeyboard`, que l'éditeur relaie :

- tant que l'interface a la **souris**, le regard ne bouge pas et la saisie d'objets est suspendue — sinon cliquer un bouton ferait aussi pivoter la caméra ;
- tant qu'elle a le **clavier**, marcher, sauter et s'accroupir sont suspendus — sinon taper un nom d'entité ferait avancer le joueur.

C'est le genre de détail qu'on ne voit pas dans une capture d'écran et qui rend un éditeur utilisable ou non.

## 1.6 La hiérarchie se déduit, elle n'est pas stockée

La scène range le lien de parenté dans l'**enfant** (`Parent { entity }`), pas dans le parent. C'est ce qui rend la mise à jour des matrices simple — chaque entité remonte sa chaîne — mais ça veut dire qu'il n'existe **aucune liste d'enfants** à parcourir.

L'arbre est donc reconstruit par balayage : pour chaque nœud, on cherche les entités qui le déclarent pour parent. C'est quadratique dans le pire des cas, et parfaitement assumé pour l'instant — une table inverse attendra qu'un niveau compte assez d'entités pour que ce parcours se voie. Le SPEC interdit d'optimiser avant d'avoir mesuré.

Détail qui compte : l'identifiant ImGui d'un nœud vient de l'**entité**, jamais de son nom. Deux entités homonymes sont légitimes — la scène a cinq caisses — et partager un identifiant les ferait s'ouvrir et se sélectionner ensemble.

## 1.7 Des degrés à l'écran, un quaternion en mémoire

L'inspecteur affiche la rotation en **degrés**. C'est ce que le SPEC annonçait dès M4, quand la physique a imposé le quaternion : personne ne raisonne en quaternions, et personne ne veut d'un format de fichier qui souffre du blocage de cardan.

La conversion n'a lieu **que si l'utilisateur touche au champ**. La refaire à chaque image ferait dériver les dernières décimales — quaternion vers Euler vers quaternion n'est pas exact — et un objet immobile finirait par tourner tout seul. C'est le même raisonnement qui avait fait rejeter « degrés dans le fichier » en M4.

## 1.8 Coût

Environ 1 Mo de binaire et une police en atlas de texture. Quelques centaines de microsecondes par image quand l'interface est affichée ; **zéro quand elle est masquée** — seule l'image ImGui vide est produite, ce qu'exige la bibliothèque et qui ne dessine rien.

L'éditeur voit les événements même masqué, et c'est voulu : sans cela, une touche relâchée pendant qu'il était caché resterait enfoncée à sa réapparition.

## 1.9 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié** : le jeu démarre avec l'éditeur, `F1` l'affiche et rend le curseur.

**Ce qui n'existe pas encore** : on ne peut ni créer, ni supprimer, ni reparenter une entité. Pas de gizmos — donc pas de manipulation directe à la souris, qui est pourtant le vrai but. Pas de sauvegarde depuis l'interface, pas de navigateur d'assets, pas de docking, et l'inspecteur ne montre que le `Transform` : les colliders, sources sonores, lumières et matériaux restent à éditer dans le fichier.

**Ce qui vient après (brique 2)** : les **gizmos**.

---

# 2. Brique 2 — les gizmos

## 2.1 Le problème

L'inspecteur permet de taper des nombres. C'est déjà mieux que d'éditer un fichier, et ce n'est pas manipuler : personne ne sait de tête qu'une poignée doit aller à `x = 0,2911` — on sait qu'elle doit aller *là*.

Un gizmo traduit ce *là*. C'est l'outil qui fait la différence entre un inspecteur et un éditeur.

## 2.2 Écrit à la main, et pourquoi

**ImGuizmo** est la bibliothèque évidente pour ça. Elle est absente du vcpkg épinglé, et surtout le SPEC exige une **validation explicite** avant toute dépendance hors de la stack verrouillée — la règle posée pour Recast/Detour en M7 vaut ici.

La translation sur trois axes tient en deux cents lignes dont l'essentiel est de la géométrie. Ce n'est pas réécrire une bibliothèque de la stack : c'est écrire le peu dont on a besoin plutôt que d'en importer beaucoup.

Le bénéfice réel n'est pas la taille du binaire, c'est que ce peu est **testable**.

## 2.3 La vraie difficulté : deux mondes, deux unités

Un gizmo traduit un mouvement de souris, qui vit en **pixels**, en un déplacement d'objet, qui vit en **mètres**. Tout le reste en découle.

Quatre fonctions isolent cette traduction, et elles sont volontairement **libres et sans état** — donc vérifiables sans fenêtre ni GPU, ce qui est la seule façon sérieuse de contrôler ce genre de calcul :

- `worldToScreen` — projeter un point. Elle **refuse** ce qui est derrière la caméra : la division perspective y rend des coordonnées parfaitement plausibles mais symétriques, et une poignée apparaîtrait à l'opposé de son objet.
- `screenRay` — l'opération inverse. Elle reconstruit deux points du même pixel, l'un au plan proche l'autre au plan lointain, et prend leur différence. C'est plus robuste que de recomposer l'œil et le champ de vision, et ça marcherait aussi en projection orthographique.
- `closestPointOnAxis` — deux droites gauches dans l'espace. Son déterminant vaut `1 − (u·v)²`, nul exactement quand elles sont parallèles : on regarde alors l'axe par la tranche, la solution part à l'infini, et **refuser est la seule réponse juste**. Rendre un nombre énorme ferait bondir l'objet à l'autre bout du niveau.
- `distanceToSegment` — le survol. Une poignée est un **segment**, pas une droite : mesurer à la droite prolongée la rendrait saisissable à l'autre bout de l'écran.

## 2.4 Trois détails qui font la différence entre un gizmo et un objet qui saute

**On mémorise où l'on a attrapé le bras.** Sans cela, l'objet sauterait à la première image pour centrer son origine sous le curseur. Le déplacement rendu est l'**écart** au point de saisie ; l'origine ayant bougé de ce qu'on a rendu à l'image précédente, cet écart retombe naturellement à zéro.

**Le survol se fige pendant la saisie.** Le curseur s'éloigne forcément du bras quand on tire dessus — perdre l'axe en cours de geste serait absurde.

**Le gizmo garde une taille constante à l'écran.** La longueur d'un bras croît avec la distance à la caméra. Un manipulateur qui rétrécit quand on recule devient inutilisable exactement au moment où l'on en a besoin, et un plancher évite qu'il ne disparaisse quand on se colle à l'objet.

## 2.5 Le monde et le repère du parent

Le gizmo calcule dans le **monde** ; le `Transform` vit dans le repère de son **parent**. La conversion n'est pas une formalité : la poignée de la scène est enfant d'une porte mise à l'échelle `(0,9 ; 2,0 ; 0,08)`. Sans conversion, la tirer l'enverrait **douze fois trop loin** sur un axe et huit fois trop court sur un autre.

L'inverse de la partie linéaire de la matrice du parent suffit — on transforme une **direction**, pas un point.

Le gizmo travaille par ailleurs selon les axes du monde et non ceux de l'objet. C'est le comportement par défaut de tous les éditeurs, et le seul qui permette d'aligner deux objets orientés différemment.

## 2.6 Qui a la souris, suite

La brique 1 relayait `WantCaptureMouse` d'ImGui. Ça ne suffit plus : le gizmo vit **dans la vue**, là où ImGui considère la souris libre. Tirer sur un bras ferait donc pivoter la caméra en même temps, et l'objet suivrait un regard qui bouge.

L'éditeur déclare donc aussi capturer la souris quand un bras est survolé ou saisi. Réciproquement, une saisie **en cours** continue même si le curseur passe sur un panneau — on ne lâche pas un objet parce qu'on a frôlé une fenêtre.

## 2.7 Coût

Trois projections et une inversion de matrice par image, uniquement quand une entité est sélectionnée. Le dessin passe par la liste d'arrière-plan d'ImGui : le gizmo se pose sur la scène mais **sous** les panneaux, ce qui évite qu'un bras ne barre l'inspecteur.

## 2.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Sept tests**, tous sans GPU : la projection et son refus de ce qui est derrière, le rayon d'écran cohérent avec elle, le point le plus proche d'un axe et son refus du cas parallèle, la distance au segment, le survol, la saisie qui déplace **sur l'axe et nulle part ailleurs**, et la taille apparente constante. **108 tests, 36104 assertions** au total.

**Ce qui n'existe pas encore** : pas de rotation ni de mise à l'échelle — seule la translation est manipulable. Pas d'aimantation sur une grille, pas d'annulation, et on ne peut pas sélectionner un objet **en cliquant dessus dans la vue** : il faut passer par la hiérarchie. Ce dernier point réutilisera `screenRay` et le lancer de rayon de M4.

---

# 3. Brique 3 — la boucle d'édition

## 3.1 Le problème

On pouvait sélectionner et déplacer. On ne pouvait ni **créer**, ni **dupliquer**, ni **détruire**, ni **enregistrer** — c'est-à-dire rien de ce qui permet de construire un niveau. Le gizmo déplaçait des objets que seul un fichier édité à la main pouvait faire exister.

## 3.2 Ces opérations appartiennent à la scène, pas à l'éditeur

Elles vivent dans `scene/editing.h` et non dans la couche éditeur, pour deux raisons.

D'abord elles n'ont **rien d'une affaire d'interface** : dupliquer une entité est une opération sur des données. Un script Lua de M7 voudra en faire autant, et il n'aura aucune raison de passer par un panneau.

Ensuite — et c'est ce qui a payé immédiatement — elles deviennent **testables sans fenêtre**. Leurs pièges ne se voient pas à l'œil.

## 3.3 Dupliquer, et ce que ça implique

**Les descendants viennent avec.** Copier une porte sans sa poignée donnerait un objet incomplet, et personne ne penserait à copier la poignée séparément.

**Les copies sont toutes créées avant qu'aucune ne soit reliée.** Un enfant peut apparaître avant son parent dans la liste ; sa copie doit déjà exister pour être désignée.

**Le lien de parenté se remappe — ou pas.** Si le parent est *dans* la copie, on relie à **sa** copie : sinon les deux arbres partageraient des enfants, et ouvrir une porte ferait bouger la poignée de l'autre. Si le parent est *à l'extérieur*, le lien se copie tel quel — dupliquer une poignée doit donner une seconde poignée sur la même porte.

**Trois choses ne sont délibérément pas copiées** : l'identifiant, qui doit rester unique parce qu'il est la clé du fichier ; la matrice monde, qui se recalcule ; et tout ce qui n'existe qu'à l'exécution — corps physique, voix audio — dont la copie désignerait la ressource de l'original. Arrêter le son de la copie couperait celui de l'autre.

## 3.4 La liste explicite, et son coût assumé

`copyDataComponents` énumère les composants un par un. EnTT sait parcourir les types d'un registre, mais pas les copier sans qu'on les ait déclarés quelque part : le choix est entre cette liste et une machinerie de réflexion. Pour une vingtaine de composants, la liste gagne.

Son coût est réel : **ajouter un composant au moteur oblige à l'ajouter ici**, sous peine qu'une duplication le perde en silence. Un test verrouille donc le contrat — il duplique une entité qui les porte tous et vérifie qu'aucun n'a été oublié.

Ce test a servi **avant même d'être fini** : `Parent` manquait à la liste, alors que c'est une donnée sérialisée. La copie n'en avait donc pas, et la relier faisait échouer une assertion d'EnTT. Le défaut a été signalé sans qu'on ait eu à lancer le jeu.

## 3.5 Détruire emporte les descendants

Détruire seulement l'entité laisserait ses enfants pointer sur un parent mort. La scène s'en remet — elle vérifie la validité avant de suivre le lien — mais les enfants **sauteraient à l'origine du monde**, ce qui a l'air d'un bug et n'en est pas un.

Les descendants partent donc en premier : détruire la racine d'abord invaliderait les liens par lesquels on les retrouve.

## 3.6 Enregistrer

Le bouton écrit vers **le fichier d'où la scène a été lue**. C'est la seule destination qui ait du sens : enregistrer ailleurs perdrait la modification au relancement suivant.

Le résultat est affiché. Une sauvegarde silencieuse laisse toujours un doute sur ce qui est parti sur le disque — et le déterminisme du format, acquis en M3, prend ici tout son sens : réenregistrer une scène qu'on n'a pas modifiée ne produit **aucun changement dans Git**.

## 3.7 Une limite à connaître

Une entité créée ou dupliquée **n'a pas de corps physique**. Les corps sont construits au chargement, à partir des `Collider` ; dupliquer une caisse donne donc un objet visible qui ne collisionne pas jusqu'au prochain lancement.

Ce n'est pas difficile à corriger — la fonction qui crée les corps ignore déjà les entités qui en ont un, donc la rappeler suffirait. Mais cela ferait dépendre l'éditeur de la physique, et cette dépendance mérite d'être décidée plutôt que subie. En attendant : enregistrer, relancer.

## 3.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Huit tests** s'ajoutent : les descendants sont trouvés à travers toute la chaîne, la duplication copie les données et donne un identifiant neuf, elle emmène les enfants en les reliant à la copie, un enfant dupliqué seul reste attaché au même parent, tous les composants de données survivent, ceux d'exécution ne sont pas copiés, détruire emporte les descendants, et détruire un enfant laisse son parent tranquille. **116 tests, 36153 assertions** au total.

**Ce qui n'existe pas encore** : on ne peut pas **reparenter** à la souris, ni choisir le maillage ou la matière d'une entité créée — elle naît invisible, et il faut encore le fichier pour lui donner un corps. L'inspecteur ne montre toujours que le `Transform` et le nom.

**Ce qui vient après (brique 4)** : l'inspecteur complet — colliders, lumières, sources sonores, et un choix de maillage et de matière parmi ce que la table de ressources connaît. C'est ce qui rendra une entité créée dans l'éditeur réellement utilisable.

---

# 4. Brique 4 — l'inspecteur complet

## 4.1 Le problème

Une entité créée dans l'éditeur naissait **invisible**. On pouvait la nommer, la déplacer, la dupliquer — mais pas lui donner un maillage, une matière, une lumière ou un collider. Il fallait rouvrir le fichier.

C'était le verrou qui empêchait de construire quoi que ce soit entièrement à la souris.

## 4.2 Énumérer les ressources revient à compter

Un choix de maillage suppose de savoir ce que le moteur connaît. La table de ressources n'exposait que la résolution nom → poignée, jamais l'inverse en bloc.

L'ajout est minuscule, et c'est une propriété du modèle qui le permet : les poignées sont des **indices consécutifs partant de zéro**. Énumérer revient donc à compter, et cinq accesseurs de taille suffisent — il n'y a rien à itérer, juste un intervalle à parcourir.

## 4.3 Ce que l'interface refuse de laisser faire

Un inspecteur n'est pas qu'un ensemble de champs : c'est aussi l'endroit où l'on empêche des états impossibles.

- **Un collider de maillage est forcé statique**, et l'interface le dit. Jolt refuse de faire bouger un maillage de triangles, qui n'a ni volume ni masse bien définis ; laisser une case à cocher promettrait quelque chose que le moteur ne tiendra pas.
- **Le cône intérieur d'un spot ne peut pas dépasser l'extérieur.** Le dégradé entre les deux s'inverserait, et le bord du faisceau deviendrait une découpe nette.
- **La masse volumique n'apparaît que pour un corps dynamique**, avec son unité. C'est le rappel qui évite de refaire l'erreur du battant de 144 kg.
- **« (aucune) » est une valeur légitime** dans chaque liste : une carte de normales est facultative, et une source audio sans son est un objet muet qu'on a le droit de vouloir.

## 4.4 Retirer un composant qu'on est en train d'afficher

Le bouton de retrait vit **dans l'en-tête du composant**, donc pendant qu'on dessine ses champs. Le supprimer sur-le-champ libérerait la mémoire que les lignes suivantes vont lire.

La demande est donc enregistrée et appliquée **après** tout le panneau. C'est le même raisonnement que la collecte avant création des corps physiques en M4 : on ne modifie pas ce qu'on est en train de parcourir.

## 4.5 Un test qui a dû être corrigé

Le garde-fou de `demo.json` vérifiait des **comptes exacts** : onze entités affichées, deux sources sonores, un collider de maillage.

Il a échoué dès le premier usage réel de l'éditeur — parce que la scène avait été modifiée et enregistrée, ce qui est précisément le travail d'un éditeur. Un test qui casse à chaque modification légitime est un test qu'on finit par désactiver, et un test désactivé ne protège plus rien.

Il vérifie désormais des **propriétés** : toute entité affichée cite une matière que le jeu connaît, toute source nomme un son enregistré, tout collider de maillage désigne une géométrie existante. Ces énoncés restent vrais quel que soit le contenu de la scène, et ce sont eux qui attrapent la vraie erreur — un nom mal orthographié.

## 4.6 Ce qui marche / ce qui ne marche pas / ce qui vient après

**116 tests, 36153 assertions**, inchangés : cette brique est de l'interface, et l'interface ne se teste pas utilement en unitaire. Ce qui se teste — l'énumération des ressources, les opérations d'édition — l'était déjà.

**Ce qui n'existe pas encore** : on ne peut pas reparenter à la souris, ni éditer les charnières, secteurs et portails, ni l'environnement de la scène. Les modifications de source audio ne prennent effet qu'au rechargement, et une entité créée n'a toujours pas de corps physique avant le prochain lancement.

**Ce qui vient après (brique 5)** : **sélectionner en cliquant dans la vue**. Passer par la liste est acceptable avec vingt entités, impraticable avec deux cents — et c'est ce qui manque pour qu'un niveau se construise au rythme de la main.

---

# 5. Brique 5 — sélectionner en cliquant

## 5.1 Le problème

Pour choisir un objet, il fallait le trouver dans la liste. C'est acceptable avec vingt entités et impraticable avec deux cents : on sait **où est** l'objet qu'on veut, pas son rang dans un arbre.

## 5.2 Une boîte suffit

Tester un rayon contre chaque triangle d'un maillage coûterait cent mille fois plus cher que contre sa boîte englobante — et n'apporterait rien. On ne demande pas au pixel près quel objet on vise : on demande **lequel est devant**.

La boîte est donc calculée une fois au chargement et retenue par la table de ressources, à côté du maillage GPU. C'est une donnée que personne n'avait jusqu'ici, parce que personne n'en avait eu besoin : le rendu n'a pas à connaître l'encombrement de ce qu'il dessine.

Ses huit coins sont transformés par la matrice monde puis réenglobés. La boîte qui en résulte est **plus large que l'objet** dès qu'il est tourné — c'est le prix d'un test aligné sur les axes, et il est sans conséquence pour désigner quelque chose.

## 5.3 La méthode des tranches, et ses deux pièges

L'intersection rayon-boîte se calcule en bornant, pour chaque axe, l'intervalle de parcours du rayon à l'intérieur de la boîte, puis en regardant si les trois intervalles se recouvrent. Quinze lignes.

Deux cas particuliers ne sont pas optionnels :

**Un rayon parallèle à une paire de faces.** La division par une direction nulle produirait un infini. Il faut traiter le cas à part : le rayon n'entre jamais par ces faces, donc soit il est déjà entre les deux, soit il les manque définitivement.

**Un rayon qui part de l'intérieur.** La distance d'entrée est alors négative, et la distance utile vaut zéro. Ce cas n'est pas théorique : il se présente **à chaque clic**, puisque la boîte de la pièce englobe la caméra.

## 5.4 Ce qui n'a rien à montrer doit rester atteignable

Une lumière, une source sonore, un secteur n'ont aucune géométrie. Ce sont pourtant les objets **les plus difficiles à retrouver dans une liste**, et ceux qu'on a le plus besoin de désigner.

Ils reçoivent donc une petite sphère posée sur leur origine — 25 cm, assez pour qu'on l'attrape, assez peu pour ne pas masquer ce qui est derrière.

## 5.5 Trois exclusions, trois raisons différentes

Un clic ne désigne pas toujours :

- **sur un panneau** — le clic appartient à l'interface ;
- **sur le gizmo** — on manipule la sélection courante, on n'en change pas. C'est pourquoi le gizmo est dessiné *avant* : il doit avoir la priorité, sans quoi attraper un bras sélectionnerait ce qu'il y a derrière ;
- **dans le vide** — et là, on **désélectionne**. C'est le geste attendu pour sortir d'une sélection, et le seul qui n'exige pas de viser autre chose.

## 5.6 Coût

Un test de boîte par entité et par clic. Pour quelques centaines d'entités, c'est instantané — et surtout, **rien ne se calcule tant qu'on ne clique pas**.

## 5.7 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Sept tests**, tous sans GPU : le rayon rencontre une boîte ou la manque, le cas parallèle ne divise pas par zéro, un rayon parti de l'intérieur donne une distance nulle, ce qui est derrière n'est jamais sélectionné, le plus proche l'emporte, une entité suit sa matrice monde, une entité mise à l'échelle est désignable sur **toute** son étendue, et ce qui n'a pas de géométrie reste atteignable. **123 tests, 36174 assertions** au total.

**Ce qui n'existe pas encore** : aucun contour ne signale l'objet sélectionné dans la vue — seuls la hiérarchie et le gizmo le montrent. Pas de sélection multiple, pas de sélection par rectangle, et un objet caché derrière un mur reste désignable si son englobant dépasse.

**Ce qui vient après** : un **niveau**. Le moteur sait désormais charger une géométrie, ses matériaux, sa collision, et l'éditer à la souris — créer, choisir, déplacer, enregistrer. Il ne lui manque plus qu'un niveau à construire.

---

# 6. Brique 6 — deux modes, pas deux couches

## 6.1 Ce que l'usage a révélé

L'éditeur était utilisable. À l'usage, cinq défauts sont remontés coup sur coup :

1. la souris ne se libérait pas — survoler la vue faisait pivoter la caméra ;
2. cliquer ne sélectionnait **que la pièce** ;
3. une porte déplacée revenait à sa place ;
4. une fois cela corrigé, elle revenait **quand même** ;
5. et une fois cela corrigé, on ne pouvait plus se déplacer.

Trois d'entre eux — le premier, le troisième et le cinquième — viennent de la **même décision** : *l'éditeur s'affiche par-dessus un jeu qui tourne*. C'était l'idée forte de la brique 1, celle qui rendait le *play-in-editor* gratuit. Elle était juste, et sa formulation était fausse.

## 6.2 La bonne formulation

L'éditeur et le jeu ne sont pas **deux couches superposées** : ce sont **deux modes**.

| | mode jeu | mode édition |
|---|---|---|
| autorité sur les poses | la simulation | la scène |
| souris | capturée en permanence | libre ; bouton droit pour regarder |
| déplacement | le personnage marche | la caméra vole |
| physique | tourne | suspendue |

Le basculement **transporte l'état** : en entrant en édition, la pose de l'objet manipulé est poussée vers son corps ; en sortant, toutes les poses éditées entrent dans la physique et la caméra retrouve le personnage.

L'avantage de départ reste entier — un seul processus, une seule scène, rien à sérialiser pour passer de l'un à l'autre. C'est l'énoncé qui a changé, pas la conception.

## 6.3 « L'interface veut la souris » n'est pas « la souris est capturée »

ImGui répond à la première question, et c'est ce que je relayais. Mais au milieu de la vue 3D, ImGui considère la souris libre — donc le jeu reprenait ses mouvements et faisait pivoter la caméra.

La question utile est la seconde. En jeu, la souris est capturée en permanence ; en édition, seulement **bouton droit maintenu** — la convention de tous les éditeurs 3D, et celle qui laisse le bouton gauche entièrement à la sélection.

Un détail qui se serait vu tout de suite : le regard n'est appliqué que si la capture était **déjà active à l'image précédente**. Le premier mouvement rapporté après une capture contient le saut du curseur vers le centre de la fenêtre, et la vue ferait un bond.

## 6.4 Une boîte qui entoure le regard ne peut pas gagner

La boîte englobante de la pièce contient tout le mobilier **et la caméra**. Un rayon parti de l'intérieur rend une distance nulle : la pièce gagnait donc à chaque clic.

J'avais pourtant écrit un test sur ce cas exact — « un rayon parti de l'intérieur donne une distance nulle ». Il était juste, et il ne disait rien de ce qu'il fallait **faire** de ce zéro. Un test peut vérifier correctement un calcul et laisser passer la décision qu'on en tire.

Une boîte qui entoure le regard est désormais un **candidat de repli** : retenue seulement si rien d'autre n'est visé. Cliquer une caisse sélectionne la caisse ; cliquer un mur sélectionne toujours la pièce.

## 6.5 Déplacer un objet contraint

Téléporter le corps ne suffisait pas : la porte a une **charnière**, ancrée dans le monde, et le solveur la ramenait au pas suivant. La contrainte faisait exactement son travail.

Deux choses manquaient donc. D'abord la **suspension de la simulation** — sans elle, rien de ce que l'éditeur écrit ne survit à l'image suivante. Ensuite la **reconstruction de la charnière** à la nouvelle place : une porte déplacée aurait continué de pivoter autour de ses anciens gonds, défaut qu'on n'aurait découvert qu'en l'ouvrant, bien après l'avoir bougée.

Le moteur physique ne savait pas retirer une contrainte : il gardait la liste des corps contraints, pas les contraintes elles-mêmes. C'est typique — on stocke ce dont on a eu besoin, et retirer n'a jamais été demandé jusqu'à ce que l'édition existe.

## 6.6 Une caméra qui vole

Suspendre la simulation fige aussi le personnage, puisque c'est elle qui le déplace. Remettre la physique en marche pour lui aurait ramené les trois défauts précédents.

Un éditeur ne fait pas marcher un personnage : il fait **voler une caméra**. La raison n'est pas seulement technique — on édite constamment des choses hors de portée d'un homme, un plafonnier, le haut d'un mur, une poutre.

Le mouvement vit dans `onFrame` et non dans le pas fixe : ce n'est pas de la simulation, rien n'en dépend, et il n'a aucune raison d'être déterministe.

## 6.7 Ce qui marche / ce qui ne marche pas

**Quatre tests** s'ajoutent : un corps se téléporte sans garder son élan, téléporter un corps statique ne déclenche pas d'assertion (Jolt refuse qu'on donne une vitesse à un mur), une charnière retirée libère le battant et refaite ailleurs le tient à ses **nouveaux** gonds, et retirer la charnière d'un objet qui n'en a pas n'emporte pas celle du voisin. **128 tests, 36194 assertions** au total.

**Ce qui n'existe pas encore** : la caméra libre n'a pas d'inertie, aucune vignette ne signale qu'on est en mode édition, et refermer l'éditeur ramène la caméra sur le personnage — prévisible, mais déroutant après avoir volé à l'autre bout du niveau.
