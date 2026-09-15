# 00 — Socle (M0)

## 1. Le problème

Avant de pouvoir dessiner quoi que ce soit à l'écran, un moteur a besoin de trois choses qui n'ont rien à voir avec le rendu :

1. **Une fenêtre** et un contexte GPU auquel dessiner (sans ça, aucune API graphique ne peut produire une image visible).
2. **Une boucle de jeu qui tourne à une vitesse stable**, indépendante du framerate réel de la machine — sinon la physique et la logique de jeu se comportent différemment selon que la machine tourne à 30 ou 240 fps.
3. **Des fondations partagées** (types, logs, assertions, mesure du temps) dont tout le reste du moteur va dépendre, donc qui doivent être posées en premier et ne plus bouger.

M0 ne fait que ça : ouvrir une fenêtre, faire tourner une boucle à pas fixe, et effacer l'écran en couleur. Rien n'est dessiné "pour de vrai" — c'est le rôle de M1 (RHI + triangle).

## 2. Les options considérées

### Boucle de jeu : pas variable vs pas fixe vs hybride

- **Pas variable** (`update(dt réel)`) : simple, mais la physique et la logique de jeu deviennent non déterministes — un `dt` différent à chaque frame change le résultat des calculs (intégration physique en particulier). Inacceptable pour un moteur qui promet une sérialisation et un comportement déterministes (règle 3 du SPEC).
- **Pas fixe pur** (`update` toujours appelé avec le même `dt`, autant de fois que nécessaire pour rattraper le temps réel) : déterministe, mais si le rendu est asservi au même pas fixe, on perd en fluidité visuelle (le rendu semble saccadé si le pas fixe est plus lent que l'écran).
- **Hybride (accumulateur + interpolation)** : la logique tourne à pas fixe (déterministe), le rendu tourne aussi vite que possible et interpole visuellement entre deux états logiques. C'est le standard de l'industrie (le "Fix Your Timestep!" de Glenn Fiedler).

**Choix : accumulateur à pas fixe**, sans l'interpolation de rendu pour l'instant (rien à interpoler tant qu'il n'y a pas de scène — ça viendra avec M2/M3). La structure `core::FixedTimestepAccumulator` est déjà conçue pour qu'on puisse ajouter l'interpolation plus tard sans la réécrire.

### Contexte GPU en M0 : RHI minimale vs appel direct

Le SPEC prévoit une RHI (abstraction GPU) à partir de M1. Utiliser la RHI dès M0 pour un simple `glClear` serait une abstraction sans second cas d'usage — contraire à la règle 5 ("pas d'interface avant deux implémentations concrètes"). **Choix : appel direct et minimal à OpenGL** (juste `glClearColor`/`glClear`, fonctions disponibles nativement sans chargeur de fonctions sur Windows) dans `platform::Application`, à jeter/remplacer entièrement quand la RHI arrive en M1.

## 3. Vocabulaire

- **Pas fixe (fixed timestep)** : la logique de jeu avance toujours par incréments de temps identiques (ex. 1/60 s), quel que soit le temps réel écoulé entre deux frames.
- **Accumulateur** : compteur de temps réel non encore "consommé" par la logique. Chaque frame, on y ajoute le temps écoulé ; tant qu'il contient au moins un pas fixe, on exécute la logique et on soustrait ce pas.
- **Spirale de la mort (spiral of death)** : si une frame met plus de temps à s'exécuter que le pas fixe, l'accumulateur grossit plus vite qu'il ne se vide, ce qui ralentit encore plus la frame suivante — effondrement en cascade. On s'en protège en plafonnant le temps ajouté par frame (`maxFrameTime`).
- **Contexte GL (OpenGL context)** : l'état interne que le driver GPU associe à une fenêtre pour y exécuter des commandes de rendu. Sans lui, aucun appel OpenGL n'est valide.
- **Profil core (core profile)** : mode d'OpenGL qui retire les fonctionnalités obsolètes (pipeline fixe). On le demande dès M0 pour ne pas prendre de mauvaises habitudes qu'il faudrait défaire en M1.

## 4. Coût

Négligeable à ce stade : pas d'allocation en boucle de frame (l'accumulateur et l'input state sont alloués une fois), pas de rendu réel donc pas de coût GPU mesurable. Le seul coût notable est la latence d'ouverture de fenêtre/contexte GL au démarrage (quelques millisecondes, hors boucle de frame).

## 5. Flux de données de M0

```
                    ┌─────────────────────────┐
                    │      game/main.cpp      │
                    │  crée platform::Application
                    └───────────┬──────────────┘
                                │ run()
                                ▼
                    ┌─────────────────────────┐
                    │   platform::Window       │
                    │   create() : SDL_Window  │
                    │   + contexte OpenGL 4.6  │
                    └───────────┬──────────────┘
                                │
          ┌─────────────────────┴─────────────────────┐
          │              boucle principale              │
          │                                              │
          │   Input::update(InputState)                  │
          │        │  pompe les évènements SDL           │
          │        ▼                                     │
          │   quitRequested ? ──oui──► sortie de boucle   │
          │        │ non                                 │
          │        ▼                                     │
          │   Clock::restart() ──► dt réel                │
          │        │                                     │
          │        ▼                                     │
          │   FixedTimestepAccumulator.addFrameTime(dt)  │
          │        │                                     │
          │        ▼                                     │
          │   while consumeStep(): onFixedUpdate()  ◄──── logique déterministe
          │        │                                     │
          │        ▼                                     │
          │   glClearColor + glClear  ◄──────────────────  "rendu" (M0 = couleur unie)
          │        │                                     │
          │        ▼                                     │
          │   Window::swapBuffers()                       │
          │                                              │
          └──────────────────────────────────────────────┘
                                │
                                ▼
                    Window::destroy() (contexte GL, fenêtre, sous-système SDL)
```

## 6. Ce qui marche / ce qui ne marche pas / ce qui vient après

**Ce qui marche (attendu)** : fenêtre qui s'ouvre, écran effacé en couleur sombre, fermeture propre sur croix / Alt+F4 / Échap, logs d'initialisation et de fermeture en console, tests doctest sur l'accumulateur.

**Ce qui ne marche pas / n'existe pas encore** : aucun rendu réel (pas de RHI), pas de gestion manette, pas d'interpolation de rendu, pas de gestion multi-fenêtre/redimensionnement.

**Ce qui vient après (M1)** : RHI minimale (buffers, textures, shaders, pipelines, passes) derrière laquelle tout appel OpenGL direct de M0 sera remplacé, triangle texturé, caméra libre.

**Vérification du build** : le socle compile sans warning (`/W4 /WX`) en Debug et en Release avec MSVC 19.38, et les tests doctest passent. La CI GitHub Actions le revérifie à chaque push. Procédure et détails dans [build-et-ci.md](build-et-ci.md).
