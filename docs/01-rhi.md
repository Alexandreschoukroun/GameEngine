# 01 — RHI (M1)

*Étape 1 : chargement d'OpenGL. Étape 2 : le premier triangle (section 7). Étape 3 : le triangle texturé (section 8).*

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

---

# 7. Étape 2 — le premier triangle

## 7.1 Le problème

Le GPU ne sait pas dessiner « un triangle ». Il sait exécuter des programmes que tu lui fournis, sur des données que tu as placées dans sa mémoire. Il manque donc trois choses :

1. **Copier les sommets dans la mémoire de la carte.** La RAM système et celle du GPU sont deux mondes séparés ; un tableau C++ n'est pas visible par le GPU.
2. **Décrire comment relire ces octets.** Le GPU reçoit des octets bruts : rien n'indique qu'il s'agit de 3 flottants par sommet.
3. **Fournir les deux étages programmables** : le *vertex shader*, exécuté une fois par sommet, et le *fragment shader*, exécuté une fois par pixel couvert.

## 7.2 Les options considérées

**Liaison globale ou accès direct (DSA)** — L'OpenGL historique impose d'attacher un objet à un point de liaison global avant de le modifier, ce qui provoque des bugs où un objet resté attaché change le comportement d'un autre code. Depuis 4.5, le **DSA** nomme l'objet directement : `glNamedBufferStorage(buffer, …)`. **Choix : DSA exclusivement**, c'est la raison d'être de l'exigence 4.6.

**Format de sommet figé ou décrit dynamiquement** — Une RHI mûre reçoit une description générique des attributs, ce qu'il faudra pour glTF en M2. Aujourd'hui il n'existe qu'un format : une position. Généraliser maintenant reviendrait à concevoir une API sans utilisateur, ce qu'interdit la règle 5 du SPEC. **Choix : un type `rhi::Vertex` figé**, qui évoluera avec les besoins réels (coordonnées de texture à l'étape 3, normales en M2).

**Sources GLSL dans le code ou dans des fichiers** — Des fichiers rechargeables à chaud supposent un système de chemins d'assets qui n'existe pas. **Choix : des chaînes dans le code du jeu**, jusqu'à la couche assets.

**Mémoire immuable ou redimensionnable** — `glNamedBufferStorage` fixe la taille définitivement, `glNamedBufferData` permet de réallouer. Nos sommets ne changeront jamais de taille. **Choix : stockage immuable.**

## 7.3 Vocabulaire

- **VBO** (*Vertex Buffer Object*) : un bloc de mémoire dans la carte graphique, ici nos 3 positions.
- **VAO** (*Vertex Array Object*) : la recette de lecture du VBO (« attribut 0 = 3 flottants, un sommet tous les 12 octets »). Une description, pas des données.
- **Vertex shader** : programme exécuté une fois par sommet. Sa sortie obligatoire `gl_Position` donne la position finale du sommet.
- **Fragment shader** : programme exécuté une fois par pixel couvert, qui sort une couleur. En plein écran 1080p, c'est environ 2 millions d'exécutions par frame.
- **Programme** : les deux étages compilés puis **liés**. C'est l'unité qu'on active pour dessiner.
- **NDC** (*Normalized Device Coordinates*) : le repère de `gl_Position`. X et Y vont de −1 à +1 quelle que soit la taille de la fenêtre. Sans caméra, on écrit les sommets directement dans ce repère.
- **Draw call** : l'ordre « dessine N sommets avec le programme actif ». C'est l'unité de coût CPU qu'on cherchera à limiter plus tard.

## 7.4 Flux de l'étape 2

```
  game/main.cpp
      │
      │  3 rhi::Vertex, en NDC          2 chaînes GLSL
      ▼                                        │
  rhi::Mesh::create                            ▼
      │                            rhi::ShaderProgram::create
      ├─ glCreateBuffers  ─────────┐           │
      ├─ glNamedBufferStorage      │    compile vertex + fragment
      │      (36 octets vers le GPU)│    lie, puis supprime les étages
      ├─ glCreateVertexArrays      │           │
      └─ glVertexArrayAttribFormat │           │  échec ⇒ le journal du
             (attribut 0 = vec3)   │           │  pilote part dans core::log
                                   │           │
                                   ▼           ▼
                            rhi::Device::draw(program, mesh)
                                   │
                                   ├─ glUseProgram
                                   ├─ glBindVertexArray
                                   └─ glDrawArrays(GL_TRIANGLES, 0, 3)
                                          │
                                          ▼
                                 swapBuffers ⇒ triangle à l'écran
```

## 7.5 Coût

3 sommets × 12 octets = **36 octets** de mémoire GPU, **un** draw call par frame, et la compilation des shaders au démarrage (quelques millisecondes, hors boucle de frame). L'enjeu de l'étape n'est pas la performance, mais d'avoir toute la plomberie posée et vérifiable à l'œil.

## 7.6 Deux choix de conception à retenir

**Aucun type OpenGL dans les en-têtes de `rhi`.** `Mesh` et `ShaderProgram` stockent leurs identifiants GPU dans des `core::u32` (un `GLuint` *est* un entier 32 bits non signé). Les en-têtes n'incluent donc pas glad, et `Device` accède aux identifiants par `friend`. La règle « pas de type GL au-dessus de rhi » tient jusque dans nos propres fichiers.

**Pas de destructeur qui appelle OpenGL.** Un objet GPU détruit après son contexte est un comportement indéfini, et l'ordre de destruction des membres serait trop facile à casser par accident. La libération est donc explicite, dans `onShutdown()`, où le contexte est encore vivant.

## 7.7 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : un triangle beige sur fond rouge très sombre, dans la fenêtre « GameEngine -- M1 », sans aucun message du debug output.

**Ce qui n'existe pas encore** : pas de texture, pas de caméra (les sommets sont écrits à la main en NDC), pas de gestion du redimensionnement de la fenêtre, pas de tampon de profondeur.

**Ce qui vient après (étape 3)** : coordonnées de texture ajoutées au sommet, envoi d'une image au GPU, échantillonnage dans le fragment shader — le triangle texturé du livrable de M1.

---

# 8. Étape 3 — le triangle texturé

## 8.1 Le problème

Un triangle d'une seule couleur ne valide presque rien. Ce qu'il faut éprouver, c'est la chaîne complète : **envoyer une image dans la mémoire du GPU, dire à chaque sommet quelle partie de l'image lui correspond, faire lire la bonne couleur par chaque pixel**. C'est ce que feront ensuite chaque mur, chaque porte et chaque objet du jeu.

Une difficulté s'y ajoute : un triangle affiché couvre rarement exactement autant de pixels que l'image a de texels. De près, un texel s'étale sur plusieurs pixels ; de loin, un pixel couvre des dizaines de texels. C'est la question du **filtrage**, et c'est elle qui sépare une texture nette d'une texture qui scintille.

## 8.2 Les options considérées

**Le contenu de l'image** — Charger un fichier supposerait `stb_image`. Mais tant qu'aucun vrai fichier n'est chargé, ce serait une dépendance sans utilisateur, donc du « pour plus tard » que le SPEC interdit. **Choix : un damier généré par le code.** Il rend visible la moindre erreur d'UV ou de déformation, et n'ajoute aucun binaire au dépôt. `stb_image` arrivera avec la couche assets.

**Le filtrage** — `GL_NEAREST` prend le texel le plus proche (net mais crénelé) ; `GL_LINEAR` fait la moyenne des 4 voisins. **Choix : linéaire, avec mipmaps** (`GL_LINEAR_MIPMAP_LINEAR`, dit trilinéaire), nécessaire dès qu'un couloir s'éloigne. À noter : c'est aussi le filtrage attendu **par défaut** par OpenGL. Sans mipmaps, la texture est *incomplète* et le triangle sort noir — un piège classique.

**L'espace colorimétrique** — Une image de couleur est presque toujours encodée en sRGB, donc non linéaire. L'échantillonner sans conversion puis l'éclairer donne un rendu faux, typiquement délavé. **Choix : ne pas traiter ce point maintenant.** Sans éclairage ni tonemapping, une demi-correction serait pire que rien. La chaîne linéaire complète est une décision de M2. Le format de stockage est donc `GL_RGBA8`, et ce paragraphe est là pour qu'on n'oublie pas la dette.

## 8.3 Vocabulaire

- **Texel** : un pixel de la texture, par opposition au pixel de l'écran.
- **UV** : coordonnées de texture attachées à chaque sommet, de 0 à 1. Le GPU les **interpole** entre les sommets, ce qui donne à chaque pixel sa position dans l'image.
- **Sampler** : la façon de lire la texture — filtrage, et comportement hors de 0..1 (ici, répétition).
- **Mipmap** : la même image pré-réduite de moitié, en cascade (256, 128, 64… 1). Le GPU choisit le niveau adapté à la distance. Coût : +33 % de mémoire ; bénéfice : plus de scintillement au loin.
- **Unité de texture** : emplacement numéroté où l'on branche une texture pour qu'un shader la lise. Ici l'unité 0, désignée côté GLSL par `layout(binding = 0)`.
- **Attributs entrelacés** : position et UV cohabitent dans le même `Vertex`, donc dans le même buffer, avec le même pas. C'est la disposition la plus favorable au cache du GPU.

## 8.4 Flux de l'étape 3

```
  game/main.cpp
      │
      ├── makeCheckerboard()  ──► 256 Ko de RGBA8 en RAM
      │                                │
      │                                ▼
      │                      rhi::Texture::create
      │                        ├─ glCreateTextures
      │                        ├─ glTextureStorage2D   (réserve 9 mipmaps)
      │                        ├─ glTextureSubImage2D  (envoie le niveau 0)
      │                        ├─ glGenerateTextureMipmap
      │                        └─ filtrage trilinéaire + répétition
      │
      └── rhi::Vertex { position, uv }
                   │
                   ▼
          attribut 0 = vec3, attribut 1 = vec2, même buffer, même pas
                   │
                   ▼
   Device::bindTexture(texture, unité 0)
                   │
                   ▼
   Device::draw  ──►  vertex shader : passe l'UV
                            │   (le GPU interpole entre les 3 sommets)
                            ▼
                      fragment shader : texture(uAlbedo, vTexCoord)
                            │
                            ▼
                      damier à l'écran
```

## 8.5 Coût

256 × 256 × 4 octets = **256 Ko**, plus environ 33 % de mipmaps, soit ~341 Ko de mémoire GPU. Génération et envoi une seule fois au démarrage. Par frame, une lecture de texture par pixel : négligeable ici, mais c'est la brique élémentaire du budget de 2,5 ms du G-buffer fixé par le SPEC.

## 8.6 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : damier beige et rouge sombre plaqué sur le triangle, arêtes nettes, aucun message du debug output.

**Dette assumée** : la conversion sRGB n'est pas faite (voir 8.2), et le redimensionnement de la fenêtre n'est toujours pas géré.

**Ce qui vient après (étape 4)** : GLM, une caméra libre, la souris capturée, et la gestion du redimensionnement — les sommets cesseront alors d'être écrits en NDC pour devenir des coordonnées du monde.
