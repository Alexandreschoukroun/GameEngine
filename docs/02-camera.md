# 02 — Maths et caméra (M1, étapes 4a et 4b)

*Étape 4a : les maths et la caméra. Étape 4b : la caméra libre (section 8).*

## 1. Le problème

Jusqu'ici, les sommets du triangle étaient écrits à la main en coordonnées normalisées : la géométrie était clouée à l'écran. Pour qu'un objet existe **dans un monde** qu'une caméra observe, il faut convertir des coordonnées exprimées en mètres vers ce repère normalisé. Cette conversion est une chaîne de multiplications par des matrices 4×4 :

```
  sommet dans          matrice        matrice        matrice
  le monde        ×    modèle    ×     vue     ×   projection   =  espace clip
  (mètres)           (où est        (où est la     (comment
                      l'objet)       caméra)        l'œil voit)
```

- La **matrice modèle** place l'objet dans le monde. Elle vaut l'identité ici : le triangle est déjà donné en coordonnées du monde.
- La **matrice vue** exprime le monde du point de vue de la caméra. Déplacer la caméra d'un mètre vers la droite revient exactement à déplacer le monde entier d'un mètre vers la gauche : c'est ce que fait cette matrice.
- La **matrice projection** applique la perspective, et élimine ce qui sort du champ.

## 2. Les décisions

**Où vivent les types mathématiques ?** Dans `core/math.h`, sous forme d'alias : `core::Vec3`, `core::Mat4`. C'est un vernis assumé — ce sont bien des types GLM qui circulent dans tout le moteur. Envelopper chaque opération coûterait cher sans rien apporter. C'est l'exception explicite à la règle 2 du SPEC : `core` étant la couche la plus basse, tout le monde est au-dessus, et les maths sont un vocabulaire commun, au même titre que `f32`. En contrepartie, GLM est lié en **`PUBLIC`** à `engine_core`, seule dépendance du projet dans ce cas.

**Quelles conventions ?** Repère main droite, Y vers le haut, caméra regardant vers **−Z** quand ses angles sont nuls, profondeur dans **[−1, +1]** (convention OpenGL). *Dette notée* : Vulkan attendra [0, 1], ce qui demandera `GLM_FORCE_DEPTH_ZERO_TO_ONE`. Oublier ce détail produit une image inversée qu'on met une journée à diagnostiquer.

**Deux angles plutôt qu'une orientation libre.** Un jeu first-person n'a jamais besoin de rouler sur le côté, et un couple yaw/pitch se borne facilement. Une orientation libre (quaternion) serait plus générale, donc plus de code pour un besoin qui n'existe pas.

**Où vit la caméra ?** Dans une nouvelle couche `engine/renderer/`, qui ne dépend que de `core`. Une caméra ne produit que des matrices : elle n'a rien à demander au GPU, donc rien à faire dans `rhi`. C'est la couche que M2 remplira avec les passes et les matériaux.

## 3. Vocabulaire

- **Espace monde** : les coordonnées réelles de la scène, en mètres.
- **Espace vue** : les mêmes points exprimés depuis la caméra, qui se retrouve à l'origine.
- **Espace clip**, puis **NDC** : le repère final, où le GPU élimine ce qui sort de l'écran.
- **FOV** : l'angle d'ouverture vertical. 60° ici, valeur usuelle en first-person.
- **Plans near et far** : distances minimale et maximale visibles (0,05 m et 100 m). Un near trop petit gaspille la précision du tampon de profondeur et provoque du *z-fighting*, ces surfaces qui clignotent.
- **Yaw / pitch** : rotations horizontale et verticale.
- **Gimbal lock** : à pitch = ±90° exactement, la direction de vue devient colinéaire à l'axe vertical, la notion de « haut » disparaît et l'image bascule. D'où le bornage à ±89°.
- **Colonne-major** : GLM range ses matrices par colonnes, comme OpenGL les attend — d'où `transpose = GL_FALSE` lors de l'envoi.

## 4. Flux de données

```
   renderer::Camera
     position, yaw, pitch            fovY, aspect, near, far
          │                                   │
          ▼                                   ▼
     glm::lookAt  ──► matrice vue      glm::perspective ──► matrice projection
          │                                   │
          └───────────────┬───────────────────┘
                          ▼
              viewProjectionMatrix()   (64 octets)
                          │
                          ▼
        ShaderProgram::setMat4(location 0, …)
          glProgramUniformMatrix4fv : désigne le programme,
          sans avoir à l'activer d'abord (même logique DSA)
                          │
                          ▼
    vertex shader : gl_Position = uViewProjection * vec4(aPosition, 1.0)
```

## 5. Coût

Deux matrices 4×4 recalculées par frame — quelques dizaines d'opérations flottantes, invisibles au profileur. 64 octets envoyés au GPU par frame. Côté GPU, une multiplication matrice-vecteur par sommet, ce que le matériel exécute par dizaines de millions par seconde.

## 6. Ce qui est enfin testable sans GPU

C'est la première partie du rendu couverte par des tests automatiques, parce que les matrices sont du calcul pur. `tests/renderer/test_camera.cpp` vérifie :

- angles nuls ⇒ la caméra regarde vers −Z, et son axe droit est +X ;
- un yaw de +90° ⇒ la caméra regarde vers +X ;
- le pitch reste borné à ±89°, même après une rotation absurde ;
- la matrice vue ramène la position de la caméra sur l'origine ;
- un point droit devant atterrit au centre de l'écran ;
- élargir la fenêtre n'étire pas le champ vertical — la régression la plus courante sur le ratio d'aspect.

Au passage, `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` a été déplacé dans `tests/main.cpp` : cette macro ne doit apparaître que dans un seul fichier, et il y en a maintenant deux.

## 7. Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : le triangle, désormais posé à 3 m devant l'origine et large de 2 m, est vu à travers une projection perspective. Il n'est plus étiré, puisque le ratio de la fenêtre entre dans le calcul.

**Ce qui n'existe pas encore** : la caméra est immobile. Rien ne lit le clavier ni la souris, et le redimensionnement de la fenêtre n'est toujours pas géré.

**Ce qui vient après (étape 4b)** : souris capturée en mode relatif, déplacement au clavier, gestion du redimensionnement — la caméra libre du livrable de M1.

---

# 8. Étape 4b — la caméra libre

## 8.1 Le problème

Trois manques, et chacun pose une question de conception.

1. **Lire le clavier sans laisser SDL fuir.** Le jeu ne peut pas écrire `SDL_SCANCODE_W` : la règle 2 l'interdit, et CMake l'empêche déjà puisque SDL est lié en `PRIVATE`.
2. **Lire la souris.** En mode normal on connaît la position du curseur, qui bute sur les bords de l'écran. Pour une caméra, ce qu'il faut, c'est le **déplacement**, sans limite.
3. **Réagir au redimensionnement.** Le viewport et le ratio d'aspect étaient fixés une fois au démarrage : agrandir la fenêtre déformait l'image.

## 8.2 Les décisions

**Scancode plutôt que keycode.** C'est la distinction utile à retenir. Le *keycode* est le symbole imprimé sur la touche (`A` sur AZERTY, `Q` sur QWERTY à la même position). Le *scancode* est la **position physique**, indépendante de la disposition.

En choisissant les scancodes, la touche que SDL nomme `W` est physiquement celle marquée **Z** sur un clavier français : les commandes sont donc naturellement **ZQSD** en France et **WASD** ailleurs, sans une ligne de configuration. C'est ce que font les jeux du commerce. Le moteur expose une énumération `platform::Key` et fait la traduction dans `input.cpp`, seul fichier du projet qui connaisse les scancodes SDL. Le remappage complet viendra avec l'accessibilité (M8).

**Souris en mode relatif.** SDL capture le curseur, le masque, et ne renvoie que des déplacements (`xrel`/`yrel`). Échap reste la sortie.

**Où appliquer la rotation du regard ?** C'est le point subtil de l'étape. Appliquée dans `onFixedUpdate`, une frame qui consomme deux pas **doublerait** la rotation, et une frame qui n'en consomme aucun la **perdrait**. Or le regard n'est pas de la simulation : c'est une entrée directe, qui doit suivre la fréquence de l'écran.

D'où un troisième point d'entrée, `onFrame(dt réel)`, appelé exactement une fois par frame. Le déplacement, lui, reste dans `onFixedUpdate` : c'est de la simulation, elle doit rester déterministe.

```
   une frame
      │
      ├── onFrame(dt réel)      ← regard souris : 1 fois, quoi qu'il arrive
      │
      ├── onFixedUpdate(1/60)   ← déplacement : 0, 1 ou n fois, déterministe
      ├── onFixedUpdate(1/60)
      │
      └── onRender()
```

**Taille en pixels, pas taille de fenêtre.** On écoute `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` et non l'évènement logique : avec une mise à l'échelle Windows à 150 %, les deux diffèrent, et c'est la taille en pixels qui intéresse le GPU.

## 8.3 Vocabulaire

- **Scancode / keycode** : position physique de la touche / symbole imprimé dessus.
- **Mode relatif** : curseur capturé et masqué, seuls les déplacements sont rapportés.
- **Sensibilité** : le facteur de conversion entre pixels de souris et radians. Ici 0,0022 rad/pixel, soit environ 360° pour 2500 pixels de déplacement.
- **Normalisation d'une direction** : sans elle, avancer en diagonale (deux touches) irait √2 fois plus vite qu'en ligne droite. C'est le bug de déplacement le plus répandu.

## 8.4 Commandes

| Touche | Action |
|---|---|
| Z Q S D (AZERTY) / W A S D (QWERTY) | Avancer, gauche, reculer, droite |
| Espace / Maj gauche | Monter, descendre |
| Souris | Regarder |
| Échap | Quitter |

## 8.5 Coût

Nul : quelques comparaisons, une normalisation de vecteur et une addition par frame.

## 8.6 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié** : la caméra se déplace et tourne, le redimensionnement de la fenêtre met à jour le viewport et le ratio sans déformer l'image.

**Ce qui n'est pas testable automatiquement** : cette étape est du branchement d'entrées, inséparable de SDL. C'est précisément pourquoi les maths de la caméra ont été isolées en 4a, où elles sont couvertes par des tests. La CI ne peut ici que compiler.

**Ce qui n'existe pas encore** : la sensibilité et les touches sont figées dans le code (options et remappage : M8), et la caméra vole librement, sans collision — le contrôleur de personnage sur capsule, c'est M4.

**Ce qui vient après (étape 5)** : Tracy, puis la clôture de M1.
