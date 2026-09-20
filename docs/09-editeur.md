# 09 — L'éditeur (M6)

*Brique 1 : l'éditeur existe — hiérarchie, inspecteur, bascule (section 1).*

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

**Ce qui vient après (brique 2)** : les **gizmos** — attraper un objet et le déplacer dans la vue. C'est ce qui transformera l'éditeur d'une liste en un outil, et ce qui aurait réglé l'affaire de la poignée en trois secondes.
