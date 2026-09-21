# 10 — Le niveau (M6.5)

Jusqu'ici, le décor du moteur tenait en une pièce : six quads écrits à la main dans
`main.cpp`, une boîte pour l'estrade, une autre pour les caisses. C'était assez pour
éprouver le rendu, la physique et le son — et parfaitement insuffisant pour éprouver un
jeu.

Ce jalon le remplace par un bâtiment : **douze pièces, 768 m², 32 × 35 m d'emprise, neuf
matières, seize embrasures.** Il s'est écrit en deux temps, et le second vient d'un retour
d'usage que la première version ne pouvait pas anticiper.

---

# 1. Brique 1 — un niveau se décrit, il ne se sculpte pas

## 1.1 Le problème

Il faut un niveau. Les trois voies possibles ne coûtent pas la même chose, et surtout
elles ne coûtent pas la même chose **la deuxième fois**.

## 1.2 Les options

**Modeler dans Blender.** C'est la voie normale d'un jeu fini, et le moteur sait déjà lire
ce qui en sort : le chargeur glTF de M5.5 ne fait aucune différence entre Suzanne et un
niveau. Mais un niveau d'apprentissage se re-règle sans arrêt — un couloir trop étroit,
un plafond trop bas, une pièce à déplacer. Dans un `.blend`, chacun de ces réglages est
une demi-heure de manipulation et une ré-exportation.

**Empiler des boîtes dans l'éditeur.** M6 vient de livrer de quoi le faire. Mais une pièce
fermée demande six boîtes, douze pièces en demandent soixante-dix, et chaque mur mitoyen
finit posé en double — deux surfaces à 0,1 mm l'une de l'autre, qui scintillent au rendu
et se contredisent en collision.

**Décrire le plan et le générer.** Un intérieur d'horreur est fait de rectangles. Un
rectangle est une **description** — quatre nombres — pas une sculpture. Et une description
se règle : élargir le couloir, ici, c'est changer un chiffre et relancer une commande.

C'est la troisième voie, dans `tools/generate_level.py`. Le jour où le niveau demandera des
moulures sculptées et des fissures, Blender reprendra la main : les deux entrent par le
même chargeur, sans qu'une ligne du moteur ne change.

## 1.3 La grille, et la règle qui tient en trois lignes

Le plan est posé sur une grille de cellules d'un mètre. Douze rectangles la remplissent :

```python
room("hall",       -7,   7,  0,  9, "carrelage", "platre_peint", 3.60, "carrelage_mural"),
room("couloir",    -2,   2,  9, 27, "carrelage", "platre_peint", 2.80, "carrelage_mural"),
room("atelier",     2,  14,  9, 19, "beton",     "brique",       4.20, None, beams=True),
...
```

Chaque cellule appartient à une pièce, et produit son sol et son plafond. Et pour les
murs, une seule règle :

```python
for cell, index in cells.items():
    for direction in DIRECTIONS:
        other = cells.get(neighbour)
        if other == index:
            continue          # même pièce des deux côtés : pas de mur
        ...
```

**Un mur sépare deux cellules dès qu'elles n'appartiennent pas à la même pièce** — qu'il y
ait une autre pièce de l'autre côté, ou le vide. Trois lignes, et les deux défauts de
l'empilement de boîtes disparaissent par construction : aucune face en double là où deux
pièces se touchent, puisque la frontière commune n'est traitée qu'une fois par côté ;
aucun trou dans l'enveloppe, puisque tout bord produit son mur.

Ce n'est pas une vérification ajoutée après coup, c'est la forme même de la boucle qui
l'interdit.

Les rectangles peuvent donc se chevaucher sans précaution : `cells.setdefault` laisse à la
cellule la pièce du premier qui l'a prise.

## 1.4 Les portes sont une conséquence du plan, pas une déclaration

La première version demandait de déclarer chaque embrasure à la main — sa cellule et sa
direction. C'était tenable à quatre portes. À seize, c'est une source d'erreurs
silencieuses : une porte oubliée donne une pièce inatteignable, et rien ne le signale.

Le générateur relève donc lui-même toutes les frontières entre deux pièces, les découpe en
tronçons continus, et **perce chaque tronçon en son milieu**. Déplacer une cloison déplace
sa porte ; ajouter une pièce lui donne son accès.

Deux réglages restent possibles, et un seul contrôle vient après :

- `SEALED` ferme un couple de pièces qui se touchent. Il sert une fois, entre l'atelier et
  la laverie : sans lui on passerait de l'un à l'autre en ligne droite, et le couloir ne
  servirait à rien. **Une contrainte de circulation est un choix de conception**, c'est
  donc le seul endroit où il faut encore décider à la main.
- `ARCHES` élargit un passage à 2,60 m et le laisse sans porte. Le hall s'ouvre ainsi sur
  son couloir : une porte d'un mètre y ferait un sas, pas une entrée.
- Puis le script **parcourt le graphe des portes depuis le hall** et nomme les pièces qu'il
  n'atteint pas. C'est la contrepartie indispensable de `SEALED` : on peut se tromper en
  fermant, on ne peut plus s'en apercevoir trop tard.

## 1.5 Une matière par fichier

Le générateur n'écrit pas un fichier, mais neuf : `niveau_beton`, `niveau_bois`,
`niveau_brique`, `niveau_carrelage`, `niveau_carrelage_mural`, `niveau_papier_peint`,
`niveau_plancher`, `niveau_platre`, `niveau_platre_peint`.

La raison est une contrainte du rendu, pas un choix d'auteur : **une entité ne porte qu'un
matériau.** Le `MeshRenderer` cite un maillage et une matière. Regrouper la géométrie **par
matière** est donc ce qui permet au hall d'être carrelé pendant que l'atelier est en béton,
et la scène place les neuf maillages à la même origine, si bien qu'ils se rejoignent
exactement.

Le prix de cette règle est une ligne à ajouter dans trois endroits — le générateur, la
table du jeu, le test — quand une matière apparaît. C'est un prix payé à la compilation
plutôt qu'à l'écran, et c'est le bon sens du marché.

## 1.6 Les UV suivent la surface, pas le plan du sol

Le piège le plus courant avec des textures libres est de les voir géantes et floues. Il
vient presque toujours du même endroit : des coordonnées de texture prises dans le plan du
sol et appliquées telles quelles aux murs.

Deux règles l'évitent :

- **Les UV d'une surface suivent cette surface.** Sur un mur perpendiculaire à X, les deux
  axes restants sont (y, z), et il faut les prendre dans l'ordre *(le long, la hauteur)* —
  les inverser coucherait la matière.
- **On divise par la taille réelle de la matière.** Une photo d'ambientCG couvre deux
  mètres de béton ; elle doit se répéter tous les deux mètres. `MATERIAL_SIZE` porte cette
  taille par matière — 1 m pour le carrelage mural, 1,2 m pour le bois, 1,6 m pour le
  papier peint, 2 m pour les maçonneries.

---

# 2. Brique 2 — pourquoi une pièce juste se lit comme une boîte

## 2.1 Le retour d'usage

La première version du niveau était géométriquement juste : pièces fermées, portes
franchissables, faces à l'endroit, aucun avertissement au chargement. Et le verdict après
essai a été : *« c'est pas réaliste, c'est tout cubique »*.

Le diagnostic mérite d'être posé précisément, parce que la réponse évidente — agrandir —
n'aurait rien réglé. **Le défaut ne venait pas de la taille, mais de l'absence de relief
d'architecture.** Un mur nu de trois mètres, une jonction sol/mur nette au millimètre, une
seule hauteur sous plafond partout : aucune de ces trois choses n'existe dans un bâtiment
réel, et leur absence se lit immédiatement, même sans qu'on sache la nommer.

Quatre corrections répondent à ça. Elles coûtent peu et se cumulent.

## 2.2 Les murs ont une épaisseur — et c'est l'embrasure qu'on en voit

Un mur d'épaisseur nulle est un plan. Le générateur en fait un **volume** de 14 cm : la
face visible depuis une pièce est en retrait de la moitié de cette épaisseur.

```python
plane = line + sign * WALL_THICKNESS / 2
```

Ce qui compte n'est pas l'épaisseur elle-même — invisible dans un mur plein — mais ce
qu'elle rend possible : **le tableau de l'embrasure.** En franchissant une porte, on longe
la tranche du mur, deux jambages et un linteau. C'est cette profondeur d'une poignée de
centimètres qui distingue un bâtiment d'un décor de théâtre, et son absence trahit le
carton plus sûrement que tout le reste.

Ces trois surfaces sont émises par `emit_reveals`, et c'est la seule partie de la géométrie
qui rende l'épaisseur visible. C'est pour elle que les murs en ont une.

## 2.2 bis Une seule épaisseur, et pourquoi — deux essais ratés

Ce paragraphe raconte un aller-retour, parce que la conclusion ne se comprend pas sans lui.

**Premier état.** Donner 30 cm à une façade et 14 à une cloison est une cote juste :
c'est ce qu'on construirait. Le résultat a été signalé après essai : **des trous dans les
murs.**

Le mécanisme est instructif parce qu'il ne se voit dans aucun contrôle évident. Prenez le
mur ouest du hall. Jusqu'à z = 7 il sépare le hall du bureau : c'est une cloison, sa face
est à 7 cm de la frontière. Au-delà, le bureau s'arrête et le même mur donne sur le vide :
sa face passait à 15 cm. **Les deux faces sont sur le même plan de mur mais pas au même
endroit**, et rien ne les reliait — une fente verticale de 8 cm, du sol au plafond, par
laquelle on voyait à travers.

Rien ne le signalait : la pièce est fermée, les faces sont à l'endroit, l'enroulement est
juste, le chargement est propre, les tests de marche passent. C'est un trou **entre** deux
surfaces correctes.

**Deuxième état, et deuxième erreur.** J'ai fermé chaque fente par un **retour** : une
bande perpendiculaire au mur, du sol au plafond. C'est également ce qu'on construirait —
un mur porteur qui rejoint une cloison présente ce ressaut — et géométriquement c'était
correct : plus aucun trou.

Verdict après essai : *« les murs sont mal superposés »*. À juste titre. J'avais rendu le
défaut **visible** au lieu de le faire disparaître : un décrochement de 8 cm en plein milieu
d'une surface plate se lit comme une erreur d'assemblage, parce que c'en est une. Dans un
vrai bâtiment, ce ressaut existe à la jonction de deux ouvrages distincts, pas au milieu
d'une paroi continue qu'on perçoit comme une seule.

**Troisième état, la racine.** La question à se poser n'était pas « comment raccorder les
deux plans » mais « qu'est-ce que la deuxième épaisseur m'apporte ». Réponse : **rien.**

On n'émet jamais la face extérieure d'une façade — personne ne voit le bâtiment de
dehors, et aucune ouverture ne la perce. L'épaisseur supplémentaire n'était donc
représentée par **aucune géométrie** : elle ne faisait que déplacer la face intérieure.
Elle ne coûtait que son défaut.

Une seule épaisseur, et les douze ressauts n'existent plus — il n'y a plus rien à
raccorder. La règle devient : **la face visible est toujours au même retrait**, donc un
mur reste d'aplomb sur toute sa longueur, même là où il change de rôle.

`find_steps` reste, mais a changé de nature : de correctif il est devenu **garde-fou**. Il
parcourt chaque plan de mur, relève les tronçons collés dont les faces ne sont pas au même
endroit, et **interrompt la génération** s'il en trouve un. Il ne répare rien : il interdit
de réintroduire la cause.

La leçon tient en une phrase. Un défaut qu'on n'arrive pas à masquer proprement vient
souvent d'une cote qu'on n'aurait pas dû prendre — et la bonne question n'est pas
« comment le cacher » mais « qu'est-ce que cette cote m'apporte ».

Le test correspondant mesure l'aplomb directement : deux rayons de part et d'autre de
chaque jonction doivent toucher **le même plan, au millimètre**. Avec l'ancien modèle,
l'écart valait 8 cm.

## 2.3 Les murs ont des profils

Plinthe en bas (14 cm, saillie 2,5 cm), cimaise à 1,15 m, corniche sous le plafond
(18 cm, saillie 5,5 cm), chambranle autour de chaque porte (11 cm, saillie 2,8 cm). Ce sont
de simples pavés posés contre le mur.

Leur relief est minuscule, et pourtant c'est lui qui fait le plus de travail, pour une
raison précise : **aucune jonction réelle entre un mur et un sol n'est nette au
millimètre.** L'œil ne sait pas énoncer cette règle, mais il la connaît, et une arête vive
sur toute la longueur d'une pièce le prévient que ce qu'il regarde n'est pas construit.

C'est aussi le poste qui domine le compte de triangles — 3 258 sur 7 510 pour le bois — et
c'est de l'argent bien dépensé.

## 2.4 Plusieurs matières sur une même hauteur de mur

Un mur du hall n'est pas d'un seul tenant : carrelage mural jusqu'à 1,15 m, cimaise, puis
plâtre peint jusqu'à la corniche. Les chambres ont un lambris de bois et du papier peint,
l'atelier de la brique nue.

Une surface uniforme sur trois mètres de haut ne se rencontre que dans un entrepôt. Cette
composition par bandes se décrit en cinq lignes :

```python
def wall_bands(place, height):
    bands = []
    low = PLINTH_HEIGHT
    if place.wainscot:
        bands.append((low, RAIL_Y, place.wainscot))
        low = RAIL_Y + RAIL_HEIGHT
    bands.append((low, height - CORNICE_HEIGHT, place.wall))
    return bands
```

Une difficulté réelle se cache derrière : une bande qui **chevauche le linteau** d'une porte
devrait être percée d'une encoche, ce qui n'est plus un rectangle. La solution est de
couper d'abord les bandes à la hauteur de chaque linteau présent sur ce mur. Après cette
coupe, chaque bande est soit entièrement sous un linteau, soit entièrement au-dessus, et
une simple soustraction d'intervalles suffit.

## 2.5 Les plafonds ne sont pas tous à la même hauteur

2,80 m dans le couloir, 3,00 dans les chambres, 3,60 dans le hall, 4,20 dans l'atelier.
C'est le **contraste** qui fait lire une architecture ; une hauteur unique fait lire une
grille.

Le mur entre deux pièces de hauteurs différentes présente à chacune sa propre hauteur — la
face côté couloir s'arrête à 2,80, celle côté atelier monte à 4,20 — et le plafond bas
couvre le raccord. Aucun cas particulier à écrire : chaque côté se génère depuis sa pièce.

S'ajoutent des **poutres** sous le plafond des deux plus grandes pièces, tous les trois
mètres. Un plafond de cent mètres carrés parfaitement nu n'existe pas, et surtout il ne
donne au regard aucune échelle.

## 2.6 Coût

| | avant | après |
|---|---|---|
| Pièces | 5 | **12** |
| Surface au sol | 182 m² | **768 m²** |
| Emprise | 16 × 21 m | **32 × 35 m** |
| Hauteurs sous plafond | 3,00 m partout | **2,80 à 4,20 m** |
| Matières | 4 | **9** |
| Embrasures | 4, déclarées à la main | **16, déduites du plan** |
| Triangles | 916 | **7 510** |
| Sur le disque | 96 Kio | **620 Kio** |
| Générateur | 352 lignes | **1244 lignes** |
| Dans le moteur | zéro ligne | **zéro ligne** |

La dernière ligne reste la plus importante. Le générateur est un outil, pas une brique : il
écrit du glTF et du JSON, que le moteur lit déjà. Il pourrait disparaître demain sans que
le niveau cesse de fonctionner.

---

# 3. Brique 3 — l'enroulement, et ce qu'il décide vraiment

## 3.1 Ce que la vérification a trouvé

Un générateur ne se trompe pas bruyamment. Il ne produit pas un message d'erreur : il
produit un mur. J'ai donc écrit un script qui relit les `.bin` produits **en dehors du
moteur** et compare, pour chaque triangle, sa normale géométrique — le produit vectoriel de
ses deux premières arêtes — à la normale déclarée dans le fichier.

Verdict du premier jet : **les 188 triangles de mur étaient enroulés à l'envers.** Aucun
message, aucun plantage, un chargement parfaitement propre.

**C'est l'enroulement qui compte, pas la déclaration :** l'élimination des faces arrière ne
regarde que lui. La réécriture a réglé le problème à la source, en donnant à chaque face
l'ordre de coins qui produit une normale sortante — c'est la table `CORNERS`, six entrées,
vérifiées une fois pour toutes.

## 3.2 La correction que l'expérience a imposée

J'avais écrit, dans le générateur puis dans le test, que des murs retournés seraient
« invisibles **et traversables** ». La première moitié est vraie. La seconde, je l'ai
vérifiée — et elle est fausse.

L'expérience : retourner volontairement les 188 triangles de mur dans le `.bin`, relancer
le test qui fait marcher un personnage dans un mur, regarder.

**Le personnage a été arrêté exactement comme avant.** Un `CharacterVirtual` de Jolt heurte
aussi les faces arrière, par défaut. Marcher dans le niveau ne prouve donc **rien** sur
l'orientation de ses faces.

C'est une leçon qui vaut au-delà de ce jalon : un test qui passe ne garde que ce qu'il
teste réellement, et la seule façon de savoir ce qu'il garde est de casser exprès ce qu'il
prétend surveiller. Sans cette contre-épreuve, j'aurais livré un test rassurant et vide.

## 3.3 Ce qui garde vraiment : la normale rapportée par un rayon

Le témoin correct, dans un test sans GPU, est un **rayon**. On en tire cinq depuis le
hall — vers trois murs, le plafond et le sol — et on vérifie deux choses :

```cpp
const physics::RayHit hit = world.raycast(middle, probe.direction, 16.0f);
REQUIRE(hit.hit);
CHECK(hit.distance == doctest::Approx(probe.distance).epsilon(0.02));
CHECK(glm::dot(hit.normal, probe.direction) < -0.99f);
```

La distance dit que la pièce est fermée, et à la bonne taille — **épaisseur des murs
comprise** : la cloison ouest du hall est attendue à 6,93 m et non à 7, parce qu'elle
présente sa face à 7 cm de la frontière. La dernière ligne est celle qui porte tout le
poids : **la surface nous fait face, sa normale remonte le rayon.**

Sur une face retournée, le rayon touche quand même — mais il revient avec une normale
opposée. Or c'est exactement la quantité dont le rendu se sert pour éliminer les faces
arrière et pour éclairer. Une normale à l'envers, c'est un mur absent à l'écran.

Contre-épreuve refaite avec la sonde : les trois murs de plâtre du hall échouent
(`dot = +1` au lieu de `−1`), le sol et le plafond — qui sont en carrelage et en béton, non
retournés — passent. Le test mord exactement là où il faut.

## 3.4 Ce que couvrent les sept tests

`tests/scene/test_niveau.cpp` éprouve le **résultat** du générateur, jamais son code :

1. tous les noms cités par la scène existent — neuf maillages, neuf matières, neuf
   collisions, les sons de pas ;
2. **les murs du hall présentent leur face avant** — la sonde ci-dessus ;
3. **les cloisons ont une épaisseur** : la même cloison sondée de ses deux côtés, depuis le
   hall puis depuis le bureau, doit rendre deux faces distantes de 14 cm et se tournant le
   dos ;
4. **le plafond de l'atelier porte des poutres** : un rayon vertical sous une poutre touche
   à 3,86 m, un rayon tiré 1,50 m à côté touche le plafond à 4,20 m. Le relief est là, et
   il n'est pas partout ;
5. le joueur tient debout sur le sol, sans le traverser ;
6. un mur l'arrête, à un rayon près ;
7. l'embrasure le laisse passer, sans dérive latérale.

Les tests 5 à 7 ne gardent pas l'enroulement, on l'a vu. Ils gardent autre chose, qui
compte tout autant : que la collision existe, qu'elle soit au bon endroit, et que le trou
soit assez large pour un corps de 70 cm de diamètre.

Le générateur est par ailleurs **déterministe** : relancé, il réécrit les mêmes octets, ce
qui rend toute modification lisible dans un diff.

---

# 4. Brique 4 — un modèle importé doit aussi collisionner

## 4.1 Le problème

`importModel` de M5.5 chargeait un glTF, en faisait un maillage GPU et des textures, puis
**laissait tomber la géométrie**. C'était sans conséquence pour Suzanne et le loquet, qui
ne sont que du décor.

Ça ne l'est plus pour un niveau : ses entités déclarent `"shape": "mesh"` et citent leur
géométrie de collision par son nom. Sans elle, la scène se charge, le niveau s'affiche —
et on le traverse de part en part.

## 4.2 La géométrie reste côté processeur

Un maillage GPU ne se relit pas. Une fois les sommets envoyés à la carte, les récupérer
demanderait un transfert en sens inverse, coûteux et à contretemps. On en garde donc une
copie là où elle est déjà : en mémoire centrale, au moment du chargement.

```cpp
out.positions = meshData.positions;
out.indices = meshData.indices;
scene::CollisionMesh collision;
collision.positions = out.positions.data();
// ...
m_resources.addCollisionMesh(file.name, collision);
```

**La copie appartient au modèle, pas à la table.** `CollisionMesh` est une *vue* : elle ne
porte que des pointeurs, exactement comme la table ne porte qu'un pointeur vers le maillage
GPU. Les tableaux doivent donc vivre au moins aussi longtemps qu'elle — d'où leur place
dans `ImportedModel`, membre de l'application.

**Positions et indices seulement.** Ni normales, ni UV, ni tangentes : la collision ne s'en
sert pas. C'est ce qui ramène la copie à un cinquième de la géométrie complète — **264 Kio**
pour les neuf maillages du niveau.

**Même nom logique pour les deux.** Le maillage d'affichage et la géométrie de collision
sont enregistrés sous `niveau_beton` tous les deux. Ce sont deux tables distinctes, donc
aucune ambiguïté, et une seule chose à écrire dans le fichier de scène.

---

# 5. Brique 5 — la provenance des matières

Neuf matières demandent neuf téléchargements, et chacun est la même suite de gestes : aller
chercher l'archive, en extraire cinq fichiers aux noms du site, renommer, convertir,
empaqueter la rugosité et la métallicité dans une seule image.

`tools/fetch_material.py` fige cette suite :

```
python tools/fetch_material.py brique Bricks075A
python tools/fetch_material.py acier  Metal046A --metal
```

Trois de ces gestes se trompent silencieusement, et c'est la raison d'être de l'outil :

- prendre la carte de normales **DirectX** au lieu de la variante OpenGL donne un relief en
  creux là où il faut une bosse — le canal vert est inversé entre les deux conventions ;
- garder la carte de normales ou la carte matière **en JPEG** y laisse des artefacts de
  compression qui se lisent comme du relief : ce sont des directions et des mesures, pas
  des photos, et elles restent en PNG ;
- oublier la **métallicité** d'un métal le rend mat comme du plastique.

L'empaquetage du canal matière reste délégué à `tools/pack_material`, écrit en C++ : il
réutilise le chargeur d'images du moteur, donc il lit exactement ce que le moteur sait
lire, et la CI le compile — il ne peut pas se décorréler du format attendu.

Accessoirement, l'outil fait que la provenance d'une matière est désormais **une ligne de
commande consultable** plutôt qu'un souvenir. Les neuf matières viennent d'ambientCG, sous
licence CC0, en 1K.

---

# 5 bis. Brique 6 — meubler avec de vrais modèles

Un bâtiment vide se lit comme un plan, pas comme un lieu. Le meubler a demandé trois
choses de natures différentes, et la distinction entre elles est le vrai sujet.

## 5bis.1 Première version : des pavés, et pourquoi ça ne suffit pas

J'ai commencé par des pavés — vingt et un meubles émis dans la géométrie du niveau, qui
héritaient ainsi de sa collision sans une ligne de plus. C'était fonctionnel et
immédiatement insuffisant : **un pavé donne une silhouette de carton, et aucune matière ne
rattrape ça.** La silhouette est ce que l'œil lit en premier, avant la texture.

Les meubles sont donc devenus de vrais modèles libres, téléchargés de Poly Haven par
`tools/fetch_model.py` : treize modèles, trente-trois meubles posés.

Quatre pavés restent, là où le catalogue libre ne couvre rien — les bacs de la laverie, les
lavabos, la chaudière, le comptoir du hall. Les remplacer le jour où un modèle apparaît ne
demande qu'une ligne.

## 5bis.2 Le générateur mesure les modèles qu'il pose

C'est la décision qui porte le plus.

**L'origine d'un modèle est arbitraire.** Elle peut être au centre, à la base, ou nulle
part : la tuyauterie industrielle a la sienne un mètre sous elle. Placer un meuble par son
origine obligerait à écrire à la main une vingtaine de décalages, qu'un simple
remplacement de modèle rendrait tous faux.

Le générateur lit donc chaque `.gltf` et le **mesure** avant de le poser. On lui donne une
empreinte au sol — un centre et un éventuel quart de tour — et il calcule le reste :

```python
placed("chambre_1", "lit", -8.5, 13.9, "lit"),
```

Un meuble repose ainsi toujours exactement sur le sol, quel que soit le modèle.

La mesure doit appliquer les **transformations de nœuds**, pas seulement lire les bornes
des accesseurs : celles-ci sont données dans le repère du maillage, et un modèle dont les
morceaux sont placés par des nœuds — un couvercle posé sur une caisse, deux battants dans
un dormant — serait mesuré à l'origine si on les ignorait. C'est d'ailleurs ce qui m'a fait
vérifier que le chargeur du moteur faisait la même chose : il appelle bien
`cgltf_node_transform_world`.

## 5bis.3 La collision suit la géométrie, et ce n'est pas un luxe

Un meuble modélisé porte un collider de **maillage**, pas de boîte. La raison est la même
que ci-dessus : une boîte de collision est centrée sur l'entité, alors que l'origine du
modèle n'est pas son centre. Une boîte serait donc décalée de la moitié du meuble.

Le collider de maillage, lui, est transformé comme la géométrie qu'il suit : il tombe
juste sans correction.

## 5bis.4 Deux garde-fous, et celui qui a servi

Une coordonnée se trompe silencieusement, donc **la génération s'arrête** si un meuble
déborde de sa pièce, traverse son plafond, ou bloque une embrasure.

Le dernier a servi dès le premier essai :

```
atelier/etabli_sud bloque une porte
```

Mon établi barrait une porte hall/atelier **que j'avais oubliée**. Les portes étant
*déduites* du plan, elles existent à des endroits qu'on ne pense pas à vérifier — et comme
le mobilier a sa collision, la pièce serait devenue **inatteignable**. Le défaut ne se
serait découvert qu'en se cognant dedans.

## 5bis.5 Les portes : une matière photographiée, et deux poignées

Quinze battants, un par baie — l'arche qui ouvre le hall sur le couloir n'en reçoit pas :
une arche est une ouverture, pas une baie.

**Rien de leur mécanique n'est du code neuf.** La charnière, le couple de frottement des
gonds et la saisie à la souris sont dans le moteur depuis M4 ; la hiérarchie qui fait
suivre la poignée vient de M3. C'est le signe que ces jalons ont été correctement
découpés.

**Le catalogue libre n'a pas de porte d'intérieur.** Poly Haven n'a qu'une porte de
château — mesurée : 2,01 × 4,06 m, double battant avec son dormant, inutilisable dans une
baie de 1,00 × 2,10. Ce qu'ambientCG appelle `Door001` n'est pas un modèle non plus mais
une **matière** : une photo de porte avec sa carte de normales, sa rugosité et sa
métallicité.

C'est pourtant la bonne réponse. Le battant reste un pavé, mais ses panneaux, ses moulures
et sa serrure sont dans le relief — et c'est le relief qu'on regarde, pas la silhouette
d'une planche qui est de toute façon plate. Le cube du jeu a des UV de 0 à 1 par face, donc
la porte s'y applique exactement une fois.

`fetch_material.py` a gagné au passage la prise en charge des **cartes** de métallicité, et
pas seulement des constantes : une porte en bois avec une serrure en laiton mélange les
deux sur la même image.

**Deux poignées, une par face.** Une porte n'en a jamais d'un seul côté ; la nôtre
disparaissait dès qu'on passait derrière le battant, ce qui trahit le décor aussi sûrement
qu'un mur troué. La seconde est la première tournée d'un demi-tour autour de Y — et cette
rotation permute les axes de la même façon que la première, si bien que l'échelle qui
annule celle du battant est **identique** pour les deux.

Trois cotes du battant ont une raison chiffrée :

- **300 kg/m³**, pas les 1000 par défaut de Jolt. Leçon de M4 : à la valeur par défaut, un
  battant pèse 120 kg et ne s'ouvre plus à la main.
- **L'ancrage de la charnière vaut −0,5**, pas −0,48 : il est exprimé dans le repère du
  battant, donc multiplié par son échelle, et tombe ainsi sur son bord quelle que soit la
  largeur choisie.
- **Le battant n'est pas un modèle.** Le moteur n'a pas besoin d'un fichier pour une
  planche ; la *poignée*, elle, en est un, parce que c'est là que la forme compte.

## 5bis.6 Ce qui se pousse aussi

Six caisses en entités dynamiques à collision de boîte — deux empilées dans l'atelier, deux
dans la réserve, une dans le hall. Elles vérifient d'un coup d'œil que la saisie à la souris
et l'empilement fonctionnent dans le **vrai** niveau.

## 5bis.7 Coût

| | avant meublage | après |
|---|---|---|
| Triangles du décor | 7 510 | **7 558** |
| Modèles importés | 2 | **15** |
| Entités dans la scène | 22 | **106** |
| Corps dynamiques | 0 | **21** |
| `assets/models/` | 0,6 Mo | **28 Mo** |
| `assets/textures/` | 29 Mo | **30 Mo** |

**Le poids est le vrai prix**, et il faut le dire : le dépôt porte maintenant une
cinquantaine de mégaoctets d'assets. Tous les modèles sont pris en 1K — une chaise qu'on
voit à deux mètres n'a pas besoin de 4096 pixels, et c'est ce qui divise le poids par
quinze. Descendre en 512 px le diviserait encore par quatre si le besoin s'en fait sentir.

# 6. Ce qui marche / ce qui ne marche pas / ce qui vient après

**Ce qui marche.** Le jeu démarre dans le hall d'un bâtiment de douze pièces **meublé**,
où quinze portes s'ouvrent à la main et six caisses se poussent. On parcourt
deux ailes et un réfectoire par un couloir de dix-huit mètres, on passe seize embrasures
dont on longe le tableau, on entend ses pas changer selon qu'on foule le carrelage ou les
planches. Les murs portent plinthe, cimaise, corniche et chambranle ; le hall a un
soubassement carrelé, les chambres un lambris, l'atelier de la brique nue sous quatre
mètres vingt et trois poutres. Douze lampes rares et chaudes laissent le reste dans le noir.

**Ce qui ne marche pas.**

- **Aucune source sonore** n'a été posée. Les sons du moteur sont synthétisés par
  `tools/generate_audio.py` et doivent être remplacés par de vrais échantillons : les
  semer dans le bâtiment avant ça reviendrait à polir ce qui va disparaître.
- **Aucun graphe de secteurs.** Le bâtiment est une seule zone : l'occlusion audio par les
  murs, qui repose sur le graphe de M3, ne s'y applique pas. C'est d'autant plus dommage
  que les cloisons ont maintenant une épaisseur qui la justifierait.
- Les portes n'ont **ni serrure ni béquille fonctionnelle** : on les pousse, on ne les
  verrouille pas. Aucune ne peut donc fermer un chemin, ce qui est pourtant le premier
  levier de progression du genre.
- **Le battant reste un pavé.** Sa matière est celle d'une vraie porte, mais vu par la
  tranche il n'a ni panneau ni moulure. Le catalogue libre n'offre aucune porte
  d'intérieur ; la produire demanderait soit de la modéliser, soit de la générer comme le
  reste du bâtiment.
- **Aucune fenêtre.** Le bâtiment est aveugle, ce qui sert le genre mais reste une limite
  de l'outil : percer un mur à mi-hauteur n'est pas prévu.
- Le plan est **écrit dans le script**, pas dans un fichier de données. Le changer demande
  de modifier du Python. Choix assumé tant qu'il n'y a qu'un niveau.
- Les douze pièces sont toutes **rectangulaires**. Ni alcôve, ni angle rentrant, ni
  escalier : la grille le permettrait, le générateur ne l'expose pas.

**Ce qui vient après.** Découper le bâtiment en **secteurs**, ce qui redonnera son sens à
l'occlusion audio de M5 et servira de base au culling — c'est d'autant plus frustrant que
le graphe existe depuis M3 et que les cloisons ont maintenant une épaisseur qui le
justifierait. Puis de vrais sons. Le reste appartient à M7.
