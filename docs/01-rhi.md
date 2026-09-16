# 01 — RHI : chargement d'OpenGL (M1, étape 1)

## 1. Le problème

Sur Windows, `opengl32.dll` n'expose que les fonctions d'**OpenGL 1.1** (1997). Tout ce qui est venu ensuite — shaders, buffers de sommets, accès direct aux objets — vit dans le pilote graphique, et doit être **demandé fonction par fonction, par son nom, au démarrage**. Sans cette étape, `glCreateBuffers` n'existe même pas à l'édition de liens.

En M0, `platform::Application` appelait directement `glClear` et `glClearColor`. C'était possible parce que ces deux fonctions font partie d'OpenGL 1.1. C'est la seule et unique raison pour laquelle ça marchait, et c'est un cul-de-sac.

Deuxième problème : **OpenGL échoue en silence**. Un argument invalide ne provoque ni plantage ni message ; l'écran reste noir. Depuis OpenGL 4.3, le pilote peut à la place appeler une fonction de notre choix à chaque erreur, avec un message en clair.

## 2. Les options considérées

- **Chargeur écrit à la main** : déclarer soi-même le type de chaque pointeur de fonction, puis appeler `SDL_GL_GetProcAddress` pour chacun. Formateur la première fois, pénible ensuite : une ligne à ajouter à chaque nouvelle fonction OpenGL utilisée, pendant toute la vie du projet.
- **GLEW** : bibliothèque historique, lourde, avec des séquelles du profil de compatibilité.
- **glad** : un générateur produit le code du chargeur pour le profil exact demandé (ici OpenGL 4.6). Une ligne d'initialisation, puis on appelle les fonctions normalement.

**Choix : glad**, disponible dans vcpkg (`glad[gl-api-46]`).

*Limite connue* : le port vcpkg de glad génère le profil **compatibility**, pas **core**. Concrètement, l'en-tête déclare aussi des fonctions obsolètes. Comme notre contexte SDL est bien un contexte core, ces fonctions ne seraient de toute façon pas chargées par le pilote. Passer le générateur en core demanderait un port personnalisé : ça n'en vaut pas la peine aujourd'hui.

## 3. Où le chargeur vit, et pourquoi

`rhi/` est la seule couche autorisée à connaître OpenGL. Mais c'est SDL, donc `platform/`, qui sait interroger le pilote. On fait donc transiter **une fonction**, pas un type :

```
  platform::Window                                   rhi::Device
  (connaît SDL, ignore OpenGL)                       (connaît OpenGL, ignore SDL)
        │                                                   ▲
        │  glProcAddressLoader()                            │
        │                                                   │
        └──►  void* (*)(const char* nom)  ──────────────────┘
                 un simple pointeur de fonction
                                    │
                                    ▼
                          gladLoadGLLoader(...)
                    charge ~2000 pointeurs de fonctions
                                    │
                                    ▼
                    glEnable(GL_DEBUG_OUTPUT) + callback
                    les erreurs du pilote arrivent dans core::log
```

Deux règles d'architecture sont ainsi tenues **par le système de build**, et pas seulement par discipline :

- `glad` est lié en `PRIVATE` à `engine_rhi` : aucune couche au-dessus ne peut inclure un en-tête OpenGL.
- `engine_platform` ne lie plus OpenGL du tout.

Conséquence directe : `platform::Application` ne dessine plus rien. Il gagne deux points d'entrée, `onInit` (fenêtre et contexte créés) et `onShutdown` (contexte encore valide), et c'est le jeu qui efface l'écran via `rhi::Device`.

## 4. Vocabulaire

- **Contexte OpenGL** : l'état que le pilote associe à une fenêtre. Toute fonction GL agit sur le contexte *courant du thread*.
- **Chargeur (loader)** : le code qui récupère l'adresse de chaque fonction moderne. En pratique, `glCreateBuffers` est un **pointeur de fonction global**, pas un symbole de la DLL.
- **Profil core** : OpenGL moderne, sans le pipeline fixe hérité.
- **Debug output** : mécanisme où le pilote appelle notre fonction à chaque erreur. `GL_DEBUG_OUTPUT_SYNCHRONOUS` force l'appel au moment exact de l'erreur, donc la pile d'appels du débogueur désigne la ligne fautive. Activé en Debug seulement.
- **Contexte de debug** (`SDL_GL_CONTEXT_DEBUG_FLAG`) : demande au pilote des messages détaillés. Sans ce drapeau, le debug output existe mais reste pauvre.
- **DSA (Direct State Access)** : manière moderne de modifier un objet GPU en le nommant directement, sans l'attacher d'abord à un point de liaison global. C'est ce qui justifie d'exiger 4.6.
- **Viewport** : la zone de la fenêtre où le GPU dessine, en pixels.

## 5. Coût

- **Mémoire / disque** : la bibliothèque glad compilée fait environ 1 Mo, liée statiquement.
- **Démarrage** : le chargement des pointeurs prend moins d'une milliseconde, hors boucle de frame.
- **Par frame** : nul. Le debug output synchrone coûte un peu de performance, d'où son activation en Debug uniquement.

## 6. Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié sur la machine de dev** (RTX 4070 Super) :

```
[19:09:15] INFO  | window created
[19:09:15] INFO  | OpenGL 4.6.0 NVIDIA 591.86 | NVIDIA GeForce RTX 4070 SUPER/PCIe/SSE2 | NVIDIA Corporation
[19:09:19] INFO  | window destroyed
[19:09:19] INFO  | application shut down cleanly
```

**Ce qui n'existe pas encore** : aucun objet GPU (ni buffer, ni shader, ni texture), donc rien n'est dessiné — l'écran est toujours effacé en couleur unie. Le viewport est fixé une fois au démarrage : le redimensionnement de la fenêtre n'est pas encore géré.

**Ce que la CI peut vérifier** : uniquement la compilation. Le runner GitHub n'a pas de carte graphique, donc ce code n'y est jamais exécuté. La validation du rendu se fait à l'œil, sur la machine de dev.

**Ce qui vient après (étape 2 de M1)** : buffer de sommets, compilation d'un programme de shaders, premier triangle en couleur unie.
