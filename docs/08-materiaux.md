# 08 — Matériaux et relief (M5.5)

*Brique 1 : les cartes de normales et les textures générées (section 1). Brique 2 : les matériaux du fichier glTF (section 2).*

Ce jalon intercalaire prépare l'éditeur de M6. Un éditeur qui ne sait éditer que des caisses grises n'apprend rien à personne : avant de pouvoir poser des objets à la souris, il faut que ces objets ressemblent à quelque chose.

# 1. Brique 1 — le relief

## 1.1 Le problème

Toutes les surfaces du moteur sont **parfaitement lisses**. Un mur de pierre, une caisse en bois et une porte renvoient la lumière exactement de la même façon, parce que l'éclairage ne connaît qu'une normale par sommet — donc une orientation qui varie doucement d'un triangle à l'autre.

Or c'est le **détail sous la lumière** qui fait reconnaître une matière. Un joint de dalle qui s'assombrit quand la torche passe, une rainure de planche qui accroche un reflet : ces indices sont plus forts que la couleur.

La solution naïve serait de modéliser ce détail en géométrie. Un sol de dalles biseautées coûterait alors des centaines de milliers de triangles, pour un relief de trois millimètres.

## 1.2 La décision : une texture qui décrit des directions

Une **carte de normales** stocke, pour chaque pixel, l'orientation de la surface à cet endroit. La géométrie reste plate ; seul l'éclairage croit à un relief.

C'est un mensonge, et il a des limites précises qu'il faut connaître : la silhouette de l'objet ne change pas, et en lumière rasante le relief s'aplatit. En échange, il coûte une lecture de texture par pixel au lieu de multiplier les triangles par mille.

## 1.3 L'espace tangent, et pourquoi il faut trois vecteurs

Une carte de normales ne peut pas stocker des directions du monde : la même texture doit fonctionner sur un sol, un mur et un plafond. Elle stocke donc des directions **relatives à la surface**, dans ce qu'on appelle l'espace tangent.

Il faut trois vecteurs pour passer de cet espace au monde :

- la **normale**, perpendiculaire à la surface, déjà là depuis M2 ;
- la **tangente**, couchée dans la surface, dans la direction où la coordonnée U augmente ;
- la **bitangente**, perpendiculaire aux deux.

La bitangente n'est **pas stockée** : elle se déduit par produit vectoriel. L'enregistrer coûterait douze octets par sommet pour une information qu'un calcul redonne. En revanche il faut un **signe**, parce que le produit vectoriel peut pointer d'un côté ou de l'autre selon l'orientation de la carte UV — c'est le `w` que glTF range dans la quatrième composante de la tangente. Sans lui, une texture miroitée produit des creux là où il faut des bosses.

```
       normale (Z)
          |
          |      surface
          +---------------- tangente (X), sens de U
         /
   bitangente (Y) = normale x tangente x w
```

## 1.4 Le piège que le format ne promet pas

Premier test écrit : *« les tangentes sont perpendiculaires aux normales »*. Échec immédiat — **77 % des sommets de Suzanne** s'en écartent, jusqu'à 0,89 de produit scalaire.

Vérification faite **hors du moteur**, en relisant le `.bin` directement : la donnée du fichier est bien comme ça. L'explication tient en une phrase : l'outil qui génère les tangentes (MikkTSpace, le standard de fait) produit une tangente **constante par triangle**, alors que les normales sont **lissées par sommet**. Sur une surface courbe, les deux divergent forcément.

C'est précisément pour ça que tout shader de normal mapping fait une **ré-orthogonalisation de Gram-Schmidt** : il retire de la tangente sa part parallèle à la normale. Ce n'est pas une précaution, c'est une étape obligatoire.

Le test vérifie donc désormais ce qui compte réellement :

- les tangentes sont **unitaires** ;
- aucune n'est **confondue** avec sa normale, sinon le repère s'effondrerait et Gram-Schmidt rendrait un vecteur nul ;
- **après correction**, la perpendiculaire est exacte.

La leçon prolonge celle des tests de charnière en M4 : *un test qui vérifie la mauvaise chose est plus dangereux que pas de test* — ici, il accusait une donnée parfaitement valide.

## 1.5 Un uniforme plutôt qu'un second shader

Tous les objets n'ont pas de carte de normales, et un maillage sans tangentes ne peut pas en recevoir. Plutôt que de compiler deux variantes du shader de géométrie, un uniforme `uNormalMapStrength` vaut 0 ou 1 et la branche du shader se coupe.

Deux programmes pour une ligne de différence multiplieraient les changements d'état GPU — l'opération la plus coûteuse d'une passe de rendu — sans rien faire gagner. Le jour où les variantes se compteront par dizaines, un système de permutations aura du sens ; pas pour une.

## 1.6 Les textures sont générées, et couleur et relief sortent du même moule

`tools/generate_textures.py` produit quatre fichiers PNG, sans aucune dépendance externe — l'écriture PNG tient en quinze lignes de `zlib` et de `struct`.

Le point important est qu'une seule **carte de hauteur** est calculée par matière, puis exploitée deux fois :

```
      relief (une hauteur par pixel)
        |
        +-- différences finies ------------> carte de normales
        +-- creux sombres / bosses claires -> couleur de base
```

Si les deux avaient été dessinées séparément, leurs joints auraient fini décalés et la surface se serait contredite elle-même — un défaut que l'œil repère instantanément sans savoir le nommer.

L'assombrissement des creux est une **occlusion ambiante du pauvre** : dans la réalité, un joint reçoit moins de lumière qu'une surface exposée. La vraie occlusion ambiante viendra plus tard ; la tirer du relief coûte trois lignes et ne peut pas se désynchroniser.

## 1.7 La force du relief se calcule

Premier réglage : une force de 90, baissée « pour modérer » à 55. Les deux étaient absurdes, et le calcul tenait en une ligne.

La hauteur varie de 1 sur un chanfrein de 14 pixels, soit environ **0,14 entre deux voisins**. Pour que la pente la plus raide atteigne 45°, il faut que la composante X de la normale y vaille 1 — donc une force d'environ **7**. J'étais un ordre de grandeur trop haut, exactement comme sur la masse de la porte en M4.

Vérification après correction : pente maximale de 58° sur la pierre, 75° dans les rainures du bois, et le fichier PNG est passé de 538 à 257 Ko — une texture calme se compresse mieux.

## 1.8 Le format n'est pas un détail

Une **couleur de base** est destinée à l'œil : ses octets suivent la courbe sRGB, et le GPU doit les ramener en linéaire à chaque lecture. Une **carte de normales** contient des directions : ses octets sont déjà proportionnels à la grandeur mesurée, toute conversion les fausserait.

Les intervertir donne soit des couleurs délavées, soit un relief penché de travers. C'est la même règle que pour la carte métallicité/rugosité depuis M2, et elle vaut pour toute texture ajoutée à l'avenir.

## 1.9 Une panne instructive

Après le changement, la scène est apparue **unie et lisse**. La cause n'était ni le shader ni la texture : le binaire n'avait pas été reconstruit.

Les shaders sont des **données**, rechargées depuis le disque à chaque lancement. Le format de sommet est du **code**. Le nouvel agencement déclare l'attribut 2 comme tangente et 3 comme coordonnées de texture ; l'ancien binaire fournissait encore les coordonnées de texture en 2. `aTexCoord` n'était donc branché sur rien et valait `(0,0)` — toute la scène échantillonnait un unique texel.

Aucune erreur, aucun avertissement : juste une image fausse. C'est le prix du rechargement des shaders sans recompilation, et il faut le connaître.

## 1.10 Vocabulaire

- **Carte de normales** : texture qui stocke une orientation par pixel, et non une couleur.
- **Espace tangent** : repère local à la surface (tangente, bitangente, normale) dans lequel ces orientations sont exprimées.
- **Gram-Schmidt** : la correction qui rend un repère perpendiculaire en retirant d'un vecteur sa part parallèle à un autre.
- **MikkTSpace** : l'algorithme standard de génération de tangentes, utilisé par Blender et par la plupart des exportateurs glTF.
- **Différences finies** : estimer une pente en comparant des voisins, faute de dérivée analytique.

## 1.11 Coût

Une lecture de texture supplémentaire par pixel visible, et seulement pour les objets qui en ont une. Seize octets de plus par sommet pour la tangente. Les quatre textures pèsent 343 Ko sur le disque, et elles sont régénérables : le script est déterministe, deux exécutions donnent les mêmes octets.

## 1.12 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié à l'écran** : la pièce est en pierre grise avec des joints sombres, les caisses et la porte en bois brun rainuré. Le relief accroche la lampe torche.

**Quatre tests** s'ajoutent, sur le chargeur : Suzanne arrive avec une tangente par sommet, les tangentes sont unitaires et forment un repère utilisable après correction, le signe de `w` vaut bien ±1, et un maillage sans tangentes reste valide alors qu'un tableau partiel est refusé. **78 tests, 508 assertions** au total.

**Ce qui n'existe pas encore** : le moteur **ignore les matériaux décrits dans le fichier glTF**. Il lit la géométrie et s'arrête ; les textures doivent être désignées à la main dans le JSON de scène. Un modèle téléchargé arrive donc avec la bonne forme et les mauvaises couleurs.

Les métaux restent noirs, faute d'environnement à réfléchir. Et il n'existe aucune occlusion ambiante réelle, ni aucune notion de matériau partagée avec la physique et l'audio — alors que les trois systèmes en réclament une depuis M4.

**Ce qui vient après (brique 2)** : lire les **matériaux glTF**.

---

# 2. Brique 2 — les matériaux du fichier

## 2.1 Le problème

Le moteur lisait la géométrie d'un fichier glTF et **s'arrêtait là**. Les matériaux que le fichier décrit — couleur de base, métallicité, rugosité, carte de normales, et les facteurs qui les multiplient — étaient purement et simplement ignorés.

Conséquence pratique : un modèle téléchargé arrivait avec la bonne forme et les mauvaises couleurs. Il fallait ouvrir le `.gltf` à la main, y lire les noms de fichiers d'images, puis les recopier dans le JSON de scène. Pour un moteur dont l'objectif affiché est qu'un non-développeur crée un jeu, c'est rédhibitoire.

## 2.2 Un matériau, pas trois textures

Jusqu'ici une entité citait trois textures indépendantes. C'était une **mauvaise description du monde** : une matière n'est pas « cette couleur avec ce relief », c'est « du bois ». Les trois textures vont ensemble, elles se changent ensemble, et rien n'empêchait d'en mélanger deux qui n'ont rien à voir.

Le `Material` regroupe donc les trois textures **et** les trois facteurs sous un nom :

```json
"mesh": { "mesh": "caisse", "material": "bois" }
```

Trois bénéfices, dans l'ordre d'importance :

- c'est l'unité que les **fichiers glTF apportent** — la correspondance est directe, sans traduction ;
- c'est l'unité que l'**éditeur de M6** manipulera : on assigne une matière à un objet, on ne lui branche pas trois images ;
- les facteurs vivent dans la table, **partagés** : régler la rugosité du bois une fois la règle sur tous les objets en bois.

## 2.3 Les facteurs multiplient, toujours

C'est une convention du format qu'il faut connaître : `baseColorFactor`, `metallicFactor` et `roughnessFactor` **multiplient** la texture correspondante, et valent 1 quand le fichier n'en dit rien.

Conséquence utile : un matériau **sans** carte de couleur mais avec un facteur rouge décrit un objet rouge uni. C'est ainsi que sont faits la plupart des modèles simples — beaucoup de fichiers n'ont aucune texture et ne sont que des facteurs.

Le shader de géométrie les applique donc systématiquement. Sur un objet texturé sans facteur particulier, la multiplication par 1 ne change rien ; sur un modèle sans texture, elle fait tout.

## 2.4 Les portions, et pourquoi elles sont nécessaires

Le chargeur fusionnait toutes les primitives en un seul maillage. C'est acceptable pour Suzanne, qui n'a qu'une matière — et faux pour presque tout le reste : un personnage a une peau, des yeux et des vêtements. Les fusionner revenait à afficher les yeux en tissu.

Chaque primitive devient donc une **portion** : même tampon de sommets, plage d'indices propre, matériau propre.

```
   un seul tampon de sommets
   [============================================]
    ^-------portion 0------^^----portion 1------^
         matériau "peau"        matériau "tissu"
```

Côté GPU, `glDrawElements` prend un décalage dans le tampon d'indices — c'est un **nombre d'octets**, pas un nombre d'indices, et c'est une source d'erreur classique. Dessiner en plusieurs appels reste bien moins cher que de dupliquer la géométrie.

Un test vérifie la propriété qui compte : les portions se suivent **sans trou ni recouvrement**. Un trou laisserait des triangles jamais dessinés, un recouvrement les dessinerait deux fois.

## 2.5 Les chemins sont relatifs au fichier, pas au répertoire courant

Un `.gltf` référence ses images par des chemins relatifs **à lui-même**. Le chargeur les résout donc à partir de l'emplacement du fichier, et rend des chemins complets.

Sans cela, un modèle rangé dans un sous-dossier ne trouverait aucune de ses images dès qu'on lance le jeu depuis ailleurs — un bug qui ne se manifeste pas sur la machine de développement, et toujours chez les autres.

Effet concret : le jeu ne cite plus qu'**une seule ligne** pour le modèle, le fichier `.gltf`. Ses textures sont déclarées dedans. Changer cette ligne suffit désormais à importer un autre modèle.

## 2.6 Ce qui reste hors de portée

Le moteur **sait** distinguer les portions et leurs matériaux, mais le jeu de démonstration n'affiche encore qu'une matière par maillage. Le pont entre les deux — créer une entité par portion à l'import — appartient à l'éditeur et à un futur import automatique.

Les `data:` URI, qui embarquent une image dans le fichier, sont reconnues et ignorées. Les extensions du format (transmission, specular/glossiness, clearcoat) le sont aussi : seul le modèle métallique/rugueux, que la spécification désigne comme standard, est lu.

## 2.7 Coût

Un appel de dessin par portion au lieu d'un par objet. Les matériaux sont lus une fois au chargement et tiennent dans quelques dizaines d'octets chacun. Trois uniformes de plus par objet, ce qui est négligeable devant un changement de texture.

## 2.8 Ce qui marche / ce qui ne marche pas / ce qui vient après

**Vérifié** : Suzanne charge ses deux textures depuis les chemins déclarés dans son propre fichier, et le jeu ne connaît plus que le `.gltf`.

**Trois tests** s'ajoutent : le matériau du fichier est lu avec ses facteurs et ses chemins résolus, les portions couvrent chaque indice exactement une fois, et un `MeshRenderer` cite un matériau qui survit à une sauvegarde-rechargement sans casser le déterminisme. **81 tests, 545 assertions** au total.

**Ce qui n'existe pas encore** : pas d'import automatique (une entité par portion), pas d'extensions glTF, pas d'images embarquées. Et surtout, **les métaux sont toujours noirs** : un métal ne fait que réfléchir son environnement, et il n'y en a pas.

**Ce qui vient après (brique 3)** : l'**éclairage d'environnement**. C'est ce qui sortira Suzanne du noir, et ce qui fera exister les métaux en général.
