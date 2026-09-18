# 04 — Renderer (M2)

*Étape 1 : profondeur, indices, faces arrière.*

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
