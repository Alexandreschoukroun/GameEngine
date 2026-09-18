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

**Ce qui vient après (étape 3)** : le G-buffer. Les shaders sortiront alors dans des fichiers `.glsl`, lus par cette même couche `assets`.
