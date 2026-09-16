# 03 — Profilage avec Tracy (M1, étape 5)

## 1. Le problème

La section 9 du SPEC fixe un budget : 16,6 ms par frame, réparti poste par poste, et « toute régression au-delà du budget se traite immédiatement, pas plus tard ». Une règle pareille n'a aucun sens sans instrument de mesure : sans chiffres, on optimise au hasard, généralement au mauvais endroit.

Le piège classique est de commencer à mesurer quand ça rame déjà. À ce moment-là, la cause est noyée dans six mois de code. Brancher le profileur dès M1, alors qu'il n'y a presque rien à mesurer, sert justement à prendre l'habitude et à voir le coût **apparaître** au fur et à mesure.

## 2. Les décisions

**Tracy est dans la stack verrouillée**, il n'y avait donc pas de choix de profileur à faire. Trois décisions restaient.

**Instrumentation plutôt qu'échantillonnage.** Un profileur par échantillonnage interrompt le programme des milliers de fois par seconde pour regarder où il en est ; il ne demande aucun travail mais donne une vue statistique, sans lien avec la notion de frame. L'instrumentation demande de marquer explicitement les zones intéressantes, et donne en échange une chronologie exacte, image par image. C'est ce qu'il faut pour tenir un budget par frame. *(Tracy sait faire les deux ; on commence par l'instrumentation.)*

**Actif en Debug comme en Release.** Un profil mesuré sur un build Debug ne dit rien du budget : MSVC y désactive l'optimisation et l'*inlining*, ce qui peut multiplier les temps par cinq ou dix. Le budget ne se vérifie qu'en Release. L'option CMake `GAMEENGINE_PROFILING` est donc à `ON` dans les deux configurations, et passera à `OFF` pour les builds distribués en M9 — le client Tracy ouvre un port réseau (8086), ce qui n'a rien à faire chez un joueur.

**Mode `on-demand`.** Sans cette option, le client enregistre dès le lancement et accumule les données en mémoire, même quand personne ne profile. Avec, il ne collecte rien tant que l'interface graphique n'est pas connectée.

**Des macros maison.** Le moteur n'appelle jamais Tracy directement : il passe par `ENGINE_PROFILE_SCOPE` et `ENGINE_PROFILE_FRAME`, définies dans `core/profiler.h`. Changer de profileur, ou désactiver l'option, ne touche qu'un fichier.

*À noter* : une macro s'expanse chez l'appelant, donc l'en-tête de Tracy doit lui être visible. Tracy est donc lié en `PUBLIC` à `engine_core`. C'est la deuxième exception assumée à la règle 2 du SPEC, après GLM, et pour la même raison : `core` est la couche la plus basse.

## 3. Vocabulaire

- **Zone** : un intervalle mesuré, du point de déclaration jusqu'à la fin de la portée C++. Les zones s'imbriquent, ce qui donne l'arbre des coûts.
- **Frame mark** : la marque de fin d'image. C'est elle qui permet à Tracy de raisonner *par frame* — temps par image, histogramme, images les plus lentes — plutôt qu'en temps absolu.
- **Client / serveur** : le client est dans ton jeu ; le serveur est l'interface graphique, un exécutable séparé qui s'y connecte par le réseau, même en local. On peut donc profiler un jeu qui tourne sur une autre machine.
- **On-demand** : le client ne collecte que pendant qu'un serveur est connecté.
- **Échantillonnage** : la méthode concurrente, statistique, qui ne demande pas d'instrumentation.

## 4. Ce qui est instrumenté

```
   une frame
      │
      ├── zone "input"          pompage des évènements SDL
      ├── zone "frame update"   onFrame : le regard à la souris
      ├── zone "fixed update"   les 0, 1 ou n pas fixes
      ├── zone "render"         onRender : les commandes GPU
      ├── zone "present"        swapBuffers
      │
      └── ENGINE_PROFILE_FRAME()   fin d'image
```

**Attention à l'interprétation de `present`.** Avec la synchronisation verticale activée, c'est là que la frame **attend l'écran**. Une zone `present` large signifie donc que le reste du travail a été fait **en avance**, pas que la présentation est lente. C'est le contresens le plus courant quand on découvre un profileur : le poste le plus large n'est pas forcément le coupable.

## 5. Comment profiler

1. Télécharger l'interface graphique de Tracy depuis les *releases* GitHub du projet (`wolfpld/tracy`), en version **0.13.1**, celle qui est épinglée dans `vcpkg.json`. Client et serveur doivent avoir la même version.
2. Lancer le jeu, de préférence en **Release**.
3. Lancer l'interface, se connecter à `localhost` (port 8086).

Le GUI n'est volontairement pas dans `vcpkg.json` : ce n'est pas une dépendance du moteur, et le compiler tirerait une dizaine de bibliothèques graphiques pour rien.

## 6. Coût

Quelques dizaines de nanosecondes par zone, et rien du tout tant qu'aucun profileur n'est connecté, grâce au mode `on-demand`. La bibliothèque cliente ajoute environ 1 Mo à l'exécutable et un thread d'écoute sur le port 8086.

## 7. Bilan de M1

Le jalon est terminé. Ce qui existe désormais :

| Brique | État |
|---|---|
| Chargement d'OpenGL 4.6, erreurs du pilote dans les logs | ✅ |
| `rhi` : buffers, VAO, shaders, textures, draw — en DSA exclusivement | ✅ |
| `renderer` : caméra perspective, couverte par des tests | ✅ |
| Caméra libre : souris relative, clavier physique, redimensionnement | ✅ |
| Profilage Tracy | ✅ |
| Livrable du SPEC : « triangle texturé, caméra libre » | ✅ |

**Les dettes assumées, à traiter en M2 :**

- la **conversion sRGB** des textures, qui n'a de sens qu'avec l'éclairage et le tonemapping ;
- la **profondeur dans [0, 1]** que demandera Vulkan, aujourd'hui en convention OpenGL ;
- l'absence de **tampon de profondeur** : avec un seul triangle, la question ne se pose pas encore ;
- les **sources GLSL dans le code**, en attendant la couche assets ;
- le port vcpkg de glad qui génère le profil *compatibility* au lieu de *core*.

**Ce qui vient après (M2 — Renderer PBR)** : chargement glTF, G-buffer deferred, lumières ponctuelles et spot, ombres, tonemapping. Livrable : une pièce éclairée par une lampe torche.
