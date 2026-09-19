# 04 — Renderer (M2)

*Étape 1 : profondeur, indices, faces arrière. Étape 2 : chargement glTF (section 2).*

Le jalon M2 vise le livrable du SPEC : **une pièce éclairée par une lampe torche**. Il se découpe en six étapes : cette première pose la 3D solide, puis viendront le chargement glTF, le G-buffer, l'éclairage PBR, les ombres, et enfin la lampe torche.

# 1. Étape 1 — profondeur, indices, faces arrière

## 1.1 Le problème

Avec un seul triangle, la question ne se posait pas. Dès qu'il y en a deux, deux problèmes apparaissent.

**Lequel est devant ?** Rien, jusqu'ici, ne déterminait l'occultation : le dernier dessiné recouvrait simplement le précédent.

**La moitié des triangles tournent le dos à la caméra.** Sur un objet fermé comme un cube, les faces arrière sont invisibles. Les rasteriser pour les recouvrir ensuite, c'est jeter la moitié du travail de fragment.

## 1.2 Les options considérées

**L'algorithme du peintre** : trier les triangles du plus lointain au plus proche avant de dessiner. Simple à énoncer, mais il échoue sur deux triangles qui s'interpénètrent — aucun ordre n'est correct — et impose un tri à chaque mouvement de caméra, donc un coût CPU croissant avec la scène.

**Le tampon de profondeur (z-buffer)** : le GPU mémorise, pour chaque pixel, la distance de ce qui y est déjà dessiné. Avant d'écrire, il compare : plus proche, on écrit et on met à jour ; plus loin, on jette. L'ordre de dessin devient indifférent, et tout est câblé dans le matériel.

**Choix : le z-buffer**, solution universelle depuis vingt-cinq ans. Ça paie au passage une dette notée à la fin de M1.

Pour les faces arrière, une seule option sérieuse : le **back-face culling**. Le GPU déduit l'orientation d'un triangle du sens de rotation de ses sommets à l'écran. C'est gratuit et ça supprime la moitié du travail de fragment sur un objet fermé.

**Les indices**, enfin. Sans eux, un cube s'écrit en 36 sommets (6 faces × 2 triangles × 3). Avec un tableau d'indices, les sommets sont stockés une fois et référencés. Subtilité : notre cube compte quand même **24 sommets, pas 8**, parce que chaque face a besoin de ses propres coordonnées de texture — un coin partagé par trois faces porte trois UV différentes.

## 1.3 Vocabulaire

- **Z-buffer / tampon de profondeur** : une image de la taille de l'écran contenant une distance par pixel, et non une couleur.
- **Depth test** (la comparaison) et **depth write** (la mise à jour) : deux choses distinctes. Pouvoir tester sans écrire servira pour la transparence, bien plus tard.
- **Z-fighting** : deux surfaces à des profondeurs indiscernables clignotent. C'est aggravé par un plan *near* trop proche, car la précision se concentre près de la caméra.
- **EBO / index buffer** : le tableau d'indices, stocké dans la carte graphique comme les sommets.
- **Winding order** : le sens de rotation des sommets d'un triangle à l'écran. Anti-horaire (CCW) = face avant, convention OpenGL par défaut.

## 1.4 Ce que fait le code

```
   platform::Window
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)
          sans cette demande, le contexte peut naître sans profondeur,
          et le test n'aurait aucun effet
                    │
                    ▼
   rhi::Device::create
      glEnable(GL_DEPTH_TEST) + glDepthFunc(GL_LESS)
      glEnable(GL_CULL_FACE)  + glCullFace(GL_BACK) + glFrontFace(GL_CCW)
                    │
                    ▼
   rhi::Mesh::create(sommets, indices)
      glCreateBuffers ×2         un buffer de sommets, un buffer d'indices
      glVertexArrayElementBuffer le VAO retient aussi le buffer d'indices,
                                 donc un seul objet à lier pour dessiner
                    │
                    ▼
   rhi::Device::clear
      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
          la profondeur est remise à 1 (le plus loin) chaque frame ;
          sans ça, la frame précédente masquerait la nouvelle
                    │
                    ▼
   rhi::Device::draw
      glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr)
          nullptr : le buffer d'indices est déjà dans le VAO
```

## 1.5 Coût

- **Mémoire** : environ 8 Mo en 1080p pour le tampon de profondeur (24 bits de profondeur, 8 de remplissage, par pixel).
- **Par frame** : le test de profondeur est câblé, donc gratuit. Le culling **supprime la moitié des fragments** d'un objet fermé — une des rares optimisations sans contrepartie.
- **Géométrie** : le cube occupe 24 × 20 octets de sommets et 36 × 4 octets d'indices, soit 624 octets.

## 1.6 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : le cube texturé s'affiche en volume, les faces proches masquent les faces lointaines, et l'intérieur n'est jamais visible depuis l'extérieur.

**Ce qui n'existe pas encore** : aucune normale sur les sommets, donc aucun éclairage possible ; la géométrie est écrite dans le code ; et la texture reste le damier généré par le code.

**Ce qui vient après (étape 2)** : le chargement de fichiers glTF avec cgltf et d'images avec stb_image — la géométrie cessera de vivre dans le C++.

---

# 2. Étape 2 — chargement glTF

## 2.1 Le problème

Un cube écrit à la main tient en quarante lignes. Suzanne, le maillage de test de Blender, en compte **11 808 sommets**. Aucun artiste ne tapera ça dans un fichier source. Il faut donc lire un format d'échange — et, avant même ça, savoir **où l'exécutable trouve ses fichiers**.

## 2.2 Les décisions

**Le format et le chargeur sont verrouillés par le SPEC** : glTF 2.0 et rien d'autre, lu par cgltf. C'est cohérent : OBJ ne transporte ni hiérarchie ni matériaux PBR, FBX est propriétaire avec un SDK de plusieurs dizaines de mégaoctets, et assimp lirait quarante formats inutiles ici pour une dépendance cent fois plus grosse que cgltf, qui tient dans un seul en-tête.

**Où vit le chargeur** : dans une nouvelle couche `engine/assets/`, qui **ne dépend que de `core`**. Elle rend des données CPU neutres — positions, UV, indices, pixels — et **ignore `rhi`**. Lire un fichier n'a aucune raison de savoir ce qu'est un buffer OpenGL. C'est l'appelant qui convertit vers le format de sommet du moteur.

**Ce qu'on lit maintenant** : positions, coordonnées de texture, indices. Le fichier contient aussi normales et tangentes, **volontairement ignorées** tant que rien ne les utilise. Les ajouter sera une ligne du chargeur à l'étape 3, quand l'éclairage arrivera.

**Où l'exécutable cherche ses données** — trois approches possibles :
- *copie par CMake à côté de l'exécutable* : identique au jeu distribué, mais toute modification d'un fichier impose de relancer le build, ce qui gênera le rechargement à chaud des shaders ;
- *chemin des sources inscrit en dur* : deux lignes, mais l'exécutable devient non déplaçable, donc il faudra une seconde logique pour M9 ;
- *résolution avec repli* : variable d'environnement, puis dossier à côté de l'exécutable, puis dossier des sources.

**Choix : la résolution avec repli.** Un seul code couvre le développement et le jeu distribué, et modifier une donnée ne demande aucune recompilation.

## 2.3 Vocabulaire

- **glTF** : « le JPEG de la 3D ». Un `.gltf` en JSON pour la structure, un `.bin` pour les données brutes, des images à côté.
- **Buffer → bufferView → accessor** : les trois niveaux de glTF. Le *buffer* est le fichier binaire, la *bufferView* en désigne une tranche, l'*accessor* dit comment la lire (type, nombre d'éléments, pas). C'est exactement la logique VBO/VAO, côté fichier.
- **Primitive** : un groupe de triangles partageant un matériau. Un mesh en contient une ou plusieurs.
- **Node** : un nœud du graphe de scène, porteur d'une transformation. L'ignorer empilerait tous les objets à l'origine.
- **Base color / albedo** : la couleur propre de la surface, sans éclairage.

## 2.4 Le piège des UV, qui n'en était pas un

glTF place l'origine des coordonnées de texture **en haut à gauche**. OpenGL la place **en bas à gauche**. Tout le monde s'attend donc à devoir retourner quelque chose.

Mais `stb_image` renvoie ses lignes **de haut en bas**, et `glTextureSubImage2D` considère la **première ligne reçue comme celle du bas**. La ligne du haut de l'image atterrit donc en `t = 0`, exactement là où glTF attend le haut de l'image. **Les deux inversions s'annulent** : il ne faut rien retourner. Vérifié à l'écran — les yeux de Suzanne sont au bon endroit.

## 2.5 Flux de données

```
   platform::assetsRoot()
      GAMEENGINE_ASSETS ?  →  assets/ à côté de l'exe ?  →  assets/ des sources
                    │
                    ▼
   assets::loadGltfMesh("models/suzanne/Suzanne.gltf")
      cgltf_parse_file      lit le JSON : structure, accessors, matériaux
      cgltf_load_buffers    charge le .bin, absent du JSON
      pour chaque node avec un mesh :
          cgltf_node_transform_world  →  matrice monde
          pour chaque primitive triangulaire :
              accessors POSITION et TEXCOORD_0 → positions et UV
              positions transformées par la matrice monde
              indices décalés du nombre de sommets déjà accumulés
                    │
                    ▼
   MeshData { positions, uvs, indices }     données neutres, en RAM
                    │
                    ▼
   game : conversion en rhi::Vertex  →  rhi::Mesh::create  →  GPU
```

## 2.6 Un détail de build qui compte

cgltf et stb_image sont des bibliothèques « à en-tête unique » : leur implémentation est générée dans **une seule** unité de compilation, par un `#define` avant l'inclusion. Comme ce code tiers est compilé dans nos fichiers, il tombe sous notre `/W4 /WX` — et il n'est pas écrit selon nos règles. Les inclusions sont donc encadrées par `#pragma warning(push, 0)` et `pop`, qui coupent les avertissements pour ces en-têtes seulement.

## 2.7 Coût

- **Disque** : 1,8 Mo pour Suzanne, dont 1,2 Mo de textures.
- **Mémoire GPU** : environ 240 Ko de maillage, 1 Mo pour la texture décompressée avec ses mipmaps.
- **Chargement** : quelques dizaines de millisecondes au démarrage, hors boucle de frame.

## 2.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : Suzanne s'affiche avec sa texture de couleur, UV correctes, sans message du debug output.

**Limites assumées** : le chargeur fusionne tout le fichier en un seul maillage et ignore les matériaux — il ne sait pas encore qu'un modèle peut avoir plusieurs textures. Les primitives non triangulaires sont rejetées. L'aspect est plat, puisqu'il n'y a aucun éclairage.

**Ce qui vient après (étape 3)** : le G-buffer. Les shaders sortiront alors dans des fichiers, lus par cette même couche `assets`.

---

# 3. Étape 3a — les shaders deviennent des données

## 3.1 Le problème

Les deux shaders vivaient dans des chaînes de caractères, au milieu de `main.cpp`. Acceptable avec deux ; intenable avec les quatre ou cinq qu'exige un G-buffer, puis avec le fragment shader d'éclairage PBR, qui fera une centaine de lignes. Trois conséquences concrètes :

- **aucune coloration syntaxique ni diagnostic** dans l'éditeur : du GLSL déguisé en littéral C++ ;
- **chaque retouche impose une recompilation du C++**, alors qu'un shader est une donnée, pas du code ;
- **les numéros de ligne des erreurs GLSL** renvoient à la chaîne, pas à un fichier qu'on peut ouvrir.

C'était la dernière dette listée au bilan de M1.

## 3.2 Les décisions

**Un shader est une donnée du jeu**, au même titre qu'une texture. Les fichiers vivent donc dans `assets/shaders/` et sont lus au démarrage par la couche `assets`, qui lit déjà les modèles et les images. Bénéfice immédiat : modifier un shader et relancer le jeu **ne demande aucune recompilation**.

**Pas de rechargement à chaud pour l'instant.** Ce serait confortable, mais il faudrait surveiller le système de fichiers et recompiler proprement en cours d'exécution. C'est une fonctionnalité d'éditeur — donc M6 — et rien ne la réclame aujourd'hui.

**L'extension `.vert` / `.frag`** plutôt que `.glsl` : les éditeurs de texte savent colorer ces extensions et reconnaissent l'étage concerné.

## 3.3 Ce que ça change dans le code

`rhi::ShaderProgram` **ne lit aucun fichier** : il reçoit toujours deux chaînes. La lecture est faite par `assets::loadTextFile`, et c'est l'appelant qui assemble les deux. La couche GPU n'a pas à connaître le système de fichiers, exactement comme la couche `assets` ignore ce qu'est un buffer OpenGL.

## 3.4 Vérification du chemin d'erreur

Testé en pointant le moteur vers un dossier de données vide :

```
[00:47:42] ERROR | fichier texte introuvable
[00:47:42] ERROR | C:\...\assets-vides\shaders\unlit.vert
[00:47:42] ERROR | initialisation de l'application echouee
```

Message explicite, chemin complet, fenêtre détruite proprement, code de retour non nul. Au passage : une variable `GAMEENGINE_ASSETS` qui désigne un dossier **inexistant** est simplement ignorée, et la résolution retombe sur le niveau suivant — c'est le comportement voulu.

## 3.5 Coût

Deux lectures de fichier au démarrage, quelques kilo-octets. Rien par frame : les shaders sont compilés une fois, à l'initialisation.

---

# 4. Étape 3b — le G-buffer

## 4.1 Le problème

Jusqu'ici, le fragment shader écrivait une couleur directement à l'écran. En ajoutant l'éclairage tel quel — c'est le rendu **forward** — chaque objet devrait évaluer **toutes les lumières** pendant qu'il se dessine :

```
   coût = (objets) × (lumières) × (pixels couverts, y compris ceux qu'un mur
                                    recouvrira une milliseconde plus tard)
```

Dans un couloir de 40 objets avec 8 lumières, cela fait 320 combinaisons, dont la majorité pour des surfaces qui finiront cachées. Le budget du SPEC ne le permet pas.

## 4.2 L'idée du rendu différé

Le travail est coupé en deux passes.

**Passe de géométrie** : chaque objet écrit ses *propriétés de surface* — couleur de base, normale, profondeur — dans des images intermédiaires. Aucun éclairage. Le tampon de profondeur fait son office : à la fin, chaque pixel contient **la surface visible, et elle seule**.

**Passe d'éclairage** : on parcourt l'écran une fois, on relit ces images, on calcule les lumières.

```
   coût = (pixels de l'écran) × (lumières)
```

Indépendant du nombre d'objets, et sans aucun calcul perdu pour des surfaces cachées.

## 4.3 Ce que le différé coûte — à connaître

- **La transparence ne passe pas.** Un G-buffer ne retient qu'une surface par pixel ; une vitre en cache une autre. Les objets transparents devront être rendus en forward, séparément, après coup.
- **Une seule « recette » de matériau.** Tout doit tenir dans le même format d'images. Acceptable ici : des murs, des portes, des objets — ni peau, ni feuillage.
- **De la bande passante.** Écrire puis relire ces images a un prix, détaillé plus bas.

Pour un jeu d'horreur en intérieur avec beaucoup de lumières locales, le compromis est très favorable.

## 4.4 Les décisions

**Contenu du G-buffer** : couleur de base en RGBA8, normale en RGBA16F, profondeur 24 bits en texture.

- **Les normales entrent enfin dans le pipeline.** Elles étaient dans le fichier glTF, volontairement ignorées faute d'utilisateur. Le G-buffer en est un.
- **Pourquoi 16 bits flottants pour la normale** : en 8 bits, les surfaces courbes montreraient des bandes. Des encodages plus compacts existent (octaédrique, par exemple) ; on y viendra si le budget le réclame.
- **La position n'est pas stockée** : elle se reconstruit depuis la profondeur et la matrice de caméra. Trois canaux économisés, et c'est la pratique standard.
- **La profondeur est une texture, pas un renderbuffer**, précisément pour pouvoir être relue.

**Un triangle plein écran plutôt qu'un rectangle.** Pour relire le G-buffer il faut couvrir l'écran. Un seul grand triangle qui déborde suffit, et ses trois sommets sont calculés dans le shader à partir de `gl_VertexID` : aucun buffer, aucun VAO à remplir, et pas de diagonale où les pixels seraient traités deux fois. Le profil core exige tout de même un VAO lié, même vide : le `Device` en garde un, inutilisé.

**Où vit le code.** `rhi::RenderTarget` est un objet GPU, donc dans `rhi`. L'orchestration des deux passes reste **dans le jeu** : quinze lignes lisibles. Elle déménagera dans `renderer/` à l'étape 4, quand l'éclairage et plusieurs lumières lui donneront une raison d'exister — pas avant.

## 4.5 Vocabulaire

- **Framebuffer** : une cible de dessin. L'écran en est un ; on peut en créer d'autres, qui écrivent dans des textures.
- **Attachement** : une texture branchée sur un framebuffer.
- **MRT** (*multiple render targets*) : un fragment shader qui écrit dans plusieurs attachements **en une seule passe**. C'est le cœur du G-buffer, activé par `glNamedFramebufferDrawBuffers`.
- **Passe de géométrie / passe d'éclairage** : les deux moitiés du rendu différé.
- **Triangle plein écran** : le triangle unique couvrant l'écran, généré sans données de sommets.
- **Profondeur non linéaire** : la valeur stockée n'est pas une distance ; la précision est concentrée près de la caméra. Affichée telle quelle, l'image paraît uniformément blanche — d'où la linéarisation dans le shader d'affichage.
- **Matrice des normales** : la transposée de l'inverse de la matrice monde. Une normale ne se transforme pas comme un point : avec une mise à l'échelle non uniforme, la matrice monde la ferait sortir de la perpendiculaire à la surface.

## 4.6 Flux de données

```
   PASSE 1 — géométrie                      cible : le G-buffer
   ┌──────────────────────────────────────────────────────────┐
   │  gbuffer.vert : monde → espace clip                       │
   │  gbuffer.frag : écrit DEUX images d'un coup (MRT)         │
   │      attachement 0  ←  couleur de base (texture du modèle)│
   │      attachement 1  ←  normale du monde, renormalisée     │
   │      profondeur     ←  écrite par le test de profondeur   │
   └──────────────────────────────────────────────────────────┘
                              │
        3 textures : albedo, normale, profondeur
                              │
   PASSE 2 — affichage                      cible : l'écran
   ┌──────────────────────────────────────────────────────────┐
   │  present.vert : 1 triangle déduit de gl_VertexID          │
   │  present.frag : relit les 3 textures                      │
   │      Tab fait défiler : couleur → normale → profondeur    │
   └──────────────────────────────────────────────────────────┘
```

À l'étape 4, la passe 2 cessera d'afficher une couche brute pour **calculer l'éclairage** à partir des trois.

## 4.7 Coût

- **Mémoire** : 4 octets de couleur + 8 de normale + 4 de profondeur = **16 octets par pixel**, soit environ **33 Mo en 1080p**. C'est le prix d'entrée du rendu différé.
- **Bande passante** : environ 12 octets écrits et 16 relus par pixel et par frame, soit ~3,5 Go/s à 60 fps en 1080p. Négligeable sur une RTX récente, à surveiller sur la GTX 1060 visée par le SPEC — c'est ce poste qu'il faudra regarder dans Tracy quand les lumières arriveront.
- **Par frame** : une passe de géométrie (identique au rendu précédent) plus une passe plein écran (un triangle, un échantillonnage par pixel).

## 4.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran, avec Tab pour faire défiler les vues** :
- *couleur de base* : identique au rendu précédent ;
- *normales* : le relief complet de Suzanne apparaît — bleu face à la caméra, rose vers +X, vert vers +Y. C'est la preuve que les normales traversent correctement toute la chaîne, du fichier glTF au G-buffer ;
- *profondeur* : le modèle sombre à 2,5 m, le fond blanc à 100 m, et les oreilles visiblement plus claires que le front parce qu'elles sont plus loin.

**Ce qui n'existe pas encore** : aucun éclairage — la passe d'affichage montre des données brutes. Le G-buffer ne contient ni rugosité ni métallicité, qui arriveront avec le PBR.

**Ce qui vient après (étape 4)** : l'éclairage PBR. La passe 2 calculera la lumière au lieu d'afficher une couche, et la conversion sRGB sera enfin traitée — la dernière dette de M1 qui ait encore un sens.

---

# 5. Étape 4a — éclairage PBR et chaîne linéaire

## 5.1 Le problème

Le G-buffer contenait tout le nécessaire, et personne ne s'en servait. Il manquait le calcul qui transforme « voici une surface » en « voici comment elle réagit à la lumière ».

Et il restait la dette repoussée deux fois : **l'espace colorimétrique**. Une image PNG n'est pas stockée proportionnellement à la lumière : elle est encodée en **sRGB**, une courbe qui donne plus de précision aux tons sombres, là où l'œil est sensible. Excellent pour stocker sur 8 bits, désastreux pour calculer. Additionner deux lumières est une addition **physique** : elle n'a de sens que sur des valeurs proportionnelles à l'énergie.

```
  texture sRGB  ──décodage──►  calculs en linéaire  ──encodage──►  écran sRGB
   (le fichier)                   (la physique)                     (les yeux)
```

Tant qu'aucun calcul n'existait, corriger n'avait aucun effet observable. Tout en dépend désormais.

## 5.2 Les décisions

**PBR plutôt que Phong.** Phong et Blinn-Phong décrivent une surface par des réglages arbitraires — une « brillance » sans unité qu'on ajuste jusqu'à ce que ça paraisse bien. Défaut connu : un matériau réglé dans un couloir éclairé paraît faux dans une pièce sombre. Le PBR décrit la matière par deux grandeurs mesurables, **rugosité** et **métallicité**, et respecte la **conservation de l'énergie** : une surface ne renvoie jamais plus qu'elle ne reçoit. Une pierre réglée une fois reste crédible sous n'importe quel éclairage — indispensable pour un jeu dont l'ambiance repose sur des lumières changeantes.

**Rugosité et métallicité empaquetées dans les canaux alpha.** Un troisième attachement aurait coûté 4 octets par pixel, soit 8 Mo en 1080p. Les canaux alpha de la couleur et de la normale étaient inutilisés :

```
  attachement 0 : RGB = couleur de base (sRGB)   A = rugosité    (linéaire)
  attachement 1 : RGB = normale du monde (16F)   A = métallicité (linéaire)
```

À noter : l'encodage sRGB ne s'applique **qu'aux canaux RGB**. L'alpha reste linéaire, il peut donc transporter une mesure sans être déformé.

**Deux formats de texture, selon ce que les octets signifient.** La couleur de base est une couleur destinée à l'œil : elle est chargée en `SRGB8_ALPHA8`, et le GPU la ramène en linéaire à chaque lecture, gratuitement. La carte métallicité/rugosité contient des **mesures** : elle est chargée en `RGBA8`, sans aucune conversion. Confondre les deux est l'erreur la plus courante de tout le sujet.

**Tonemapping ACES.** L'éclairage produit des valeurs sans plafond ; l'écran s'arrête à 1. Couper brutalement crame les hautes lumières en blanc plat. Une approximation de la courbe ACES, six lignes, préserve le détail et donne un contraste cinématographique — ce qu'on veut pour de l'horreur.

## 5.3 Vocabulaire

- **BRDF** : la fonction qui répond à « une lumière arrive de cette direction, combien repart vers l'œil ? ».
- **Métallicité** : 0 = diélectrique (bois, pierre, plastique), 1 = métal. Un métal n'a **pas de composante diffuse** et teinte ses reflets.
- **Rugosité** : 0 = miroir, 1 = parfaitement mat. Elle contrôle l'étalement du reflet.
- **Fresnel** : tout matériau devient réfléchissant quand on le regarde en rasant.
- **Microfacettes** : la surface est modélisée comme une multitude de micro-miroirs. **GGX** décrit leur distribution (terme D), le terme géométrique (G) l'ombre qu'ils se portent entre eux.
- **HDR** : des valeurs d'éclairage sans plafond, avant réduction à l'écran.
- **Reconstruction de position** : retrouver le point du monde correspondant à un pixel à partir de sa profondeur et de l'inverse de la matrice de caméra — la raison pour laquelle la position n'est pas stockée.

## 5.4 La découverte de l'étape : un métal sans environnement est noir

Le premier rendu est apparu presque entièrement noir. Vérification faite dans le fichier : **le matériau de Suzanne est déclaré 100 % métallique** (canal bleu de la carte à 255), avec une rugosité de 0,32.

Le rendu était donc **correct**. Un métal n'a aucune composante diffuse : il ne fait que réfléchir ce qui l'entoure. Ici, l'environnement est le vide absolu — il ne reste que le reflet direct de la lampe.

Vérifié en forçant temporairement la métallicité à zéro : le modèle apparaît alors normalement éclairé, avec un dégradé diffus propre et des reflets sur les arcades. La chaîne complète fonctionne.

**Conséquence à retenir pour la suite** : un moteur PBR sans éclairage d'environnement rend les métaux inutilisables. Ce n'est pas bloquant pour un jeu d'horreur en intérieur sombre, mais la question se posera — probablement à M8, avec l'ambiance.

## 5.5 Artefact connu

De fines mouchetures blanches suivent les coutures d'UV du modèle. Cause probable : aux coutures, le filtrage de la carte de rugosité interpole vers des valeurs proches de zéro, donc vers un miroir parfait, qui renvoie un reflet extrêmement intense sur un seul pixel. C'est un défaut classique du PBR (*specular aliasing*), traité habituellement par un plancher de rugosité plus haut ou un filtrage spécifique. Noté, non corrigé : il faudra voir s'il subsiste sur de vrais assets de décor.

## 5.6 Coût

Aucun coût mémoire supplémentaire, grâce à l'empaquetage. Par pixel d'écran : une reconstruction de position, puis une trentaine d'opérations flottantes par lumière. En 1080p avec une lumière, environ 60 millions d'opérations par frame — une fraction de milliseconde. C'est le poste « éclairage » du budget du SPEC, et il grossira à chaque lumière ajoutée.

## 5.7 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : éclairage correct avec atténuation en 1/d², reflets cohérents avec la rugosité, tonemapping sans zone cramée, et Tab donne toujours accès aux trois couches brutes du G-buffer.

**Ce qui n'existe pas encore** : une seule lumière, aucune ombre — tout objet est éclairé même s'il est derrière un mur. Aucun éclairage d'environnement. Le code d'orchestration est toujours dans le jeu.

**Ce qui vient après (étape 4b)** : plusieurs lumières, et le déménagement des passes dans `renderer/` — l'éclairage multiple lui donnera enfin une raison d'exister.

---

# 6. Étape 4b — plusieurs lumières, et le renderer prend sa place

## 6.1 Le problème

Trois choses convergent.

**Une seule lumière ne démontre rien.** Tout l'intérêt du rendu différé est que le coût de l'éclairage ne dépend pas de la géométrie. Avec une lampe, c'est une affirmation théorique.

**La scène n'a rien à éclairer.** Un objet flottant dans le vide ne montre ni la portée d'une lampe ni son atténuation. Et une ombre sans sol pour la recevoir est invisible — ce sera bloquant dès l'étape 5.

**Le code d'orchestration a atteint sa limite.** Quinze lignes dans `onRender` restaient lisibles. Avec une liste de lumières à téléverser, plusieurs objets à parcourir et bientôt des ombres, ça ne l'est plus.

## 6.2 Les décisions

**Tableau d'uniformes plutôt que buffer de stockage.** Un tableau fixé à **8 lumières** — la valeur exacte du budget du SPEC — se téléverse en deux envois et se lit directement dans le shader. Un SSBO permettrait des milliers de lumières, mais n'a d'intérêt qu'accompagné d'un découpage de l'écran en tuiles, qu'on n'a aucune raison d'écrire aujourd'hui. Remplacer ce plafond touchera deux fichiers.

*Piège à connaître* : un tableau d'uniformes occupe autant d'emplacements consécutifs qu'il a d'éléments. `uLightPositions` déclaré à l'emplacement 7 consomme les emplacements 7 à 14 ; les couleurs commencent donc à 15. Se tromper ici écrase silencieusement une autre variable.

**La frontière du renderer.** Le jeu décrit **quoi** dessiner et **quelles** lumières existent ; le renderer décide **comment** — passes, cibles, uniformes, ordre des opérations. `onRender` du jeu tient maintenant en une liste et un appel. C'est le déménagement annoncé à l'étape 3b, fait au moment où il se justifie et pas avant.

**Le renderer charge ses propres shaders.** Il dépend donc de `assets` et `platform`, mais en `PRIVATE` : ceux qui l'utilisent n'ont pas à connaître le système de fichiers. `rhi` est en revanche `PUBLIC`, puisque son API manipule des maillages et des textures.

**Un matériau constant tient dans une texture de 1×1 pixel.** Le sol n'a pas de carte de matière : plutôt que d'ajouter des paramètres de matériau à toute la chaîne, sa rugosité et sa métallicité sont stockées dans une texture d'un seul pixel. Le shader ne fait aucune différence, et c'est une technique courante dans les vrais moteurs.

## 6.3 Vocabulaire

- **Draw item** : le couple géométrie + textures transmis au renderer. L'ancêtre de ce que la scène de M3 produira automatiquement depuis les entités.
- **Tableau d'uniformes** : variable de shader indexée, de taille fixée à la compilation.
- **Rendu par tuiles / clustered** : découpage de l'écran pour ne tester que les lumières pertinentes par zone. Hors sujet à 8 lumières, indispensable à 500.

## 6.4 Ce que l'image démontre

Trois lumières de couleurs différentes — ambre, bleue, rouge — posent trois flaques distinctes sur le sol en damier, avec une atténuation douce et des recouvrements crédibles. Suzanne, métallique, reste sombre avec ses reflets : cohérent avec la découverte de l'étape 4a.

Le point important n'est pas esthétique : **passer de une à trois lumières n'a rien changé au nombre d'objets dessinés**. La passe de géométrie est identique ; seule la boucle de la passe d'éclairage s'est allongée. C'est exactement la propriété qu'on cherchait.

## 6.5 Coût

Chaque lumière ajoute une trentaine d'opérations par pixel d'écran. À 8 lumières en 1080p, environ 500 millions d'opérations par frame : c'est le poste « éclairage » de 3,5 ms du budget du SPEC. Les deux passes sont maintenant instrumentées séparément dans Tracy (`gbuffer pass` et `lighting pass`), ce qui permettra de voir laquelle grossit.

## 6.6 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : trois lumières colorées, un sol diélectrique qui rend enfin le diffus lisible, et les vues de débogage toujours accessibles par Tab.

**L'artefact de mouchetures blanches** signalé à l'étape 4a réapparaît sur le sol, aux angles rasants. C'est bien du *specular aliasing* : à faible rugosité apparente et en incidence rasante, un seul pixel peut recevoir un reflet extrêmement intense. Toujours noté, toujours pas corrigé.

**Ce qui n'existe pas encore** : aucune ombre. Les lumières traversent Suzanne comme si elle n'existait pas, et c'est très visible sur le sol.

**Ce qui vient après (étape 5)** : les ombres. C'est le défaut le plus criant de l'image actuelle, et le dernier obstacle avant la lampe torche.

---

# 7. Étape 5 — les ombres portées

## 7.1 Deux choses différentes s'appellent « ombre »

**L'ombre propre** fonctionnait déjà : une surface qui ne fait pas face à la lampe ne reçoit rien. C'est le terme `max(dot(normal, light), 0.0)` — dos tourné à la lumière, zéro. C'est pourquoi l'arrière d'un objet est noir.

**L'ombre portée** manquait : une surface qui fait face à la lampe, mais dont quelque chose bloque le chemin. Le sol sous Suzanne regarde bien le projecteur, donc il recevait sa pleine lumière alors que Suzanne était entre les deux. Le shader ne pouvait pas le savoir : il traite chaque pixel isolément, sans rien connaître du reste de la scène.

## 7.2 L'idée : rendre la scène depuis la lumière

On place une caméra **à l'endroit de la lampe**, on dessine la scène en n'enregistrant que la profondeur, et on obtient une image qui dit « dans cette direction, le premier obstacle est à telle distance ».

Ensuite, pour chaque pixel de l'écran, on transforme sa position du monde dans le repère de la lampe :

```
   distance réelle du pixel à la lampe   >   distance enregistrée ?
            │                                        │
            └─ oui : quelque chose est devant  →  dans l'ombre
               non : rien ne s'interpose       →  éclairé
```

La visibilité se ramène à **une comparaison de profondeurs**.

## 7.3 La décision d'ordre : le spot d'abord

Un spot est un cône : **une seule** carte de profondeur suffit. Une lumière ponctuelle éclaire dans toutes les directions et en demanderait **six**, une par face d'un cube — six fois le coût de rendu par lampe, et beaucoup plus de code.

**Choix : le spot d'abord**, parce que c'est exactement ce dont la lampe torche a besoin à l'étape 6, et que le SPEC en fait un citoyen de première classe. Les deux lumières ponctuelles d'ambiance restent sans ombre.

## 7.4 Les trois pièges classiques

**L'acné d'ombre.** La carte a une résolution finie : un texel couvre plusieurs centimètres de sol. Un point comparé à la profondeur enregistrée de son voisin se déclare dans l'ombre de lui-même, et la surface se couvre de rayures. Le remède est un **biais**, qu'on rend **proportionnel à l'inclinaison** de la surface par rapport à la lampe, car c'est là que l'erreur grandit :

```glsl
float bias = max(0.0015 * (1.0 - nDotL), 0.0004);
```

**Le peter-panning.** Un biais trop grand décolle l'ombre de l'objet, qui semble flotter — comme l'ombre de Peter Pan. Tout l'art consiste à prendre le plus petit biais qui supprime l'acné.

**Les bords en escalier.** La carte est une texture, ses pixels se voient. Deux remèdes cumulés ici :
- la **comparaison matérielle** (`sampler2DShadow`) : le GPU effectue le test de profondeur *dans le filtrage de texture*, donc chaque lecture est déjà la moyenne de quatre comparaisons voisines, gratuitement ;
- le **PCF 3×3** : neuf de ces lectures, moyennées.

## 7.5 Deux détails qui font échouer silencieusement

**Hors du cône, la carte ne sait rien.** La texture est bordée d'une profondeur maximale (`CLAMP_TO_BORDER` avec une bordure à 1), ce qui signifie « rien ne t'obstrue ». Sans ça, tout ce qui déborde du cône serait noir.

**La passe d'ombre n'écrit aucune couleur.** `glNamedFramebufferDrawBuffer(fbo, GL_NONE)` : sans cette ligne, le framebuffer serait jugé *incomplet* et la passe échouerait. Son fragment shader est littéralement vide — le GPU ne remplit que la profondeur, ce qui rend la passe très rapide.

## 7.6 Vocabulaire

- **Shadow map** : la carte de profondeur vue depuis la lampe.
- **Espace de la lumière** : le repère où la lampe est la caméra.
- **Biais pentu** (*slope-scaled bias*) : décalage appliqué avant comparaison, proportionnel à l'inclinaison.
- **PCF** (*percentage-closer filtering*) : moyenne de plusieurs comparaisons voisines.
- **Spot** : lumière en cône, définie par une direction et deux demi-angles — plein éclairage à l'intérieur du premier, extinction progressive jusqu'au second.

## 7.7 Flux de la frame, désormais à trois passes

```
   PASSE 0 — ombre                   cible : la shadow map (1024², profondeur seule)
       la scène redessinée depuis la lampe, sans texture ni éclairage
                              │
   PASSE 1 — géométrie               cible : le G-buffer
       couleur, normale, rugosité, métallicité des surfaces visibles
                              │
   PASSE 2 — éclairage               cible : l'écran
       pour chaque lumière : cône, atténuation, BRDF
       pour la lumière à ombre : comparaison dans la shadow map
```

## 7.8 Coût

- **Mémoire** : 3 Mo pour une carte de 1024×1024 en 24 bits. Doubler la résolution **quadruple** la mémoire.
- **Par frame** : une passe de géométrie supplémentaire par lumière à ombre — sans texture ni éclairage, donc rapide.
- **Par pixel éclairé** : neuf lectures de texture, chacune déjà filtrée par le matériel.

C'est le poste « ombres » de 3,5 ms du budget du SPEC, et c'est lui qui limitera le nombre de lumières bien avant le calcul d'éclairage.

## 7.9 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : la silhouette de Suzanne se projette sur le damier, bords adoucis, sans rayures d'acné ni décollement.

**Ce qui n'existe pas encore** : une seule lumière à ombre à la fois, et uniquement de type spot. Les lumières ponctuelles n'en ont pas. La résolution de la carte est fixe, quelle que soit la portée de la lampe.

**Ce qui vient après (étape 6)** : la lampe torche — et c'est le livrable de M2. Un spot attaché à la caméra, avec son cône, son ombre, et les détails qui font qu'elle paraît tenue à la main plutôt que vissée sur le front.
