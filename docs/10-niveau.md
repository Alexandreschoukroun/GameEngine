# 10 — Le niveau (M6.5)

Jusqu'ici, le décor du moteur tenait en une pièce : six quads écrits à la main dans
`main.cpp`, une boîte pour l'estrade, une autre pour les caisses. C'était assez pour
éprouver le rendu, la physique et le son — et parfaitement insuffisant pour éprouver un
jeu. Ce jalon remplace cette pièce unique par un vrai plan : cinq salles, un couloir,
quatre embrasures, quatre matières au sol.

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

**Empiler des boîtes dans l'éditeur.** M6 vient de livrer de quoi le faire : créer,
déplacer, dupliquer, enregistrer. Mais une pièce fermée demande six boîtes, cinq pièces
en demandent trente, et chaque mur mitoyen finit posé en double — deux surfaces à
0,1 mm l'une de l'autre, qui scintillent au rendu et se contredisent en collision.

**Décrire le plan et le générer.** Un intérieur d'horreur est fait de rectangles. Un
rectangle est une **description** — quatre nombres — pas une sculpture. Et une description
se règle : élargir le couloir, ici, c'est changer un chiffre et relancer une commande.

C'est la troisième voie qui est retenue, dans `tools/generate_level.py`. Le jour où le
niveau demandera des moulures et des fissures, Blender reprendra la main : les deux entrent
par le même chargeur, sans qu'une ligne du moteur ne change.

## 1.3 La grille, et la règle qui tient en trois lignes

Le plan est posé sur une grille de cellules d'un mètre. Cinq rectangles la remplissent :

```python
ROOMS = [
    Room("hall",     -4, 4,  0,  6, "carrelage"),
    Room("couloir",  -1, 1,  6, 16, "carrelage"),
    Room("chambre",  -8, -1, 8, 14, "plancher"),
    Room("atelier",   1, 8,  9, 15, "plancher"),
    Room("reserve",  -3, 3, 16, 21, "beton"),
]
```

Chaque cellule occupée produit son sol et son plafond. Et pour les murs, une seule règle :

```python
for (x, z) in cells:
    for direction in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if (x + direction[0], z + direction[1]) in cells:
            continue          # deux cellules voisines : pas de mur entre elles
        emit_wall(mesh, x, z, direction, ...)
```

**Un mur n'existe que là où une cellule occupée borde le vide.** Trois lignes, et les deux
défauts de l'empilement de boîtes disparaissent par construction : aucune face en double
là où deux pièces se touchent, puisque la frontière commune n'est bordée par aucun vide ;
aucun trou, puisque tout bord de vide produit son mur. Ce n'est pas une vérification
ajoutée après coup, c'est la forme même de la boucle qui l'interdit.

Les pièces peuvent donc se chevaucher sans précaution. Le couloir traverse le hall :
`cells.setdefault` laisse à la cellule la matière du premier qui l'a prise, et le
carrelage du hall reste continu.

Une embrasure se déclare par la cellule qu'elle perce et la direction du percement :

```python
DOORWAYS = [((0, 5), (0, 1)), ((-1, 10), (-1, 0)), ((0, 11), (1, 0)), ((0, 15), (0, 1))]
```

Le pan de mur concerné n'est alors pas émis d'une pièce, mais en trois morceaux : deux
jambages et un linteau au-dessus de 2,10 m. Le trou est un vrai trou — pas un quad
transparent.

## 1.4 Une matière par fichier, et pourquoi

Le générateur n'écrit pas un fichier, mais quatre : `niveau_beton`, `niveau_platre`,
`niveau_carrelage`, `niveau_plancher`.

La raison est une contrainte du rendu, pas un choix d'auteur : **une entité ne porte qu'un
matériau.** Le `MeshRenderer` cite un maillage et une matière. Un sol carrelé et un
parquet dans le même fichier obligeraient à trancher entre les deux. Regrouper la
géométrie **par matière** est donc ce qui permet au hall d'être carrelé pendant que
l'atelier est en planches — et la scène place les quatre maillages à la même origine,
si bien qu'ils se rejoignent exactement.

Le découpage se retrouve tel quel côté collision, pour une raison différente : la
collision suit ici exactement la géométrie visible. Rien n'y oblige — le collider d'un
décor est d'ordinaire plus grossier que son apparence — mais à 916 triangles, la
simplifier ne rapporterait rien.

## 1.5 Les UV suivent la surface, pas le plan du sol

Le piège le plus courant avec des textures libres est de les voir géantes et floues. Il
vient presque toujours du même endroit : des coordonnées de texture prises dans le plan
du sol et appliquées telles quelles aux murs.

Deux règles l'évitent ici :

- **Les UV d'un mur suivent le mur** : sa longueur et sa hauteur, pas les coordonnées X/Z
  de la cellule. Un mur de 3 m de haut reçoit trois mètres de matière verticalement.
- **On divise par la taille réelle de la matière.** Une photo d'ambientCG couvre deux
  mètres de béton ; elle doit donc se répéter tous les deux mètres. `MATERIAL_SIZE` porte
  cette taille par matière — 1,5 m pour le plancher, 2 m pour les autres — et le
  générateur divise les UV par elle.

Le résultat se lit dans la vérification : le béton se répète 8 fois en X et 10,5 fois en
Z sur une emprise de 16 × 21 m. C'est bien un motif tous les deux mètres.

## 1.6 Coût

| | |
|---|---|
| Géométrie | 916 triangles, 1 832 sommets, 4 fichiers |
| Sur le disque | 96 Kio (glTF + binaires) |
| Plan | 5 pièces, 182 m² au sol, 4 embrasures |
| Générateur | 348 lignes de Python, aucune dépendance |
| Dans le moteur | **zéro ligne** |

La dernière ligne est la plus importante. Le générateur est un outil, pas une brique : il
écrit du glTF et du JSON, que le moteur lit déjà. Il pourrait disparaître demain sans que
le niveau cesse de fonctionner.

---

# 2. Brique 2 — l'enroulement, et ce qu'il décide vraiment

## 2.1 Ce que la vérification a trouvé

Un générateur ne se trompe pas bruyamment. Il ne produit pas un message d'erreur : il
produit un mur. J'ai donc écrit un script qui relit les `.bin` produits **en dehors du
moteur** et compare, pour chaque triangle, sa normale géométrique — le produit vectoriel
de ses deux premières arêtes — à la normale déclarée dans le fichier.

Verdict du premier jet : **les 188 triangles de mur étaient enroulés à l'envers.** Aucun
message, aucun plantage, un chargement parfaitement propre.

Le correctif est d'une ligne, et son commentaire dit pourquoi :

```python
corners = corners[::-1]
```

`wall_corners` donnait les coins dans l'ordre qui produit une normale géométrique opposée
à celle qu'on déclarait à côté. **C'est l'enroulement qui compte, pas la déclaration :**
l'élimination des faces arrière ne regarde que lui.

## 2.2 La correction que l'expérience a imposée

J'avais écrit, dans le générateur puis dans le test : « des murs enroulés à l'envers
seraient invisibles de l'intérieur **et traversables** ». La première moitié est vraie. La
seconde, je l'ai vérifiée — et elle est fausse.

L'expérience : retourner volontairement les 188 triangles de mur dans le `.bin`, relancer
le test qui fait marcher un personnage dans un mur, regarder.

**Le personnage a été arrêté exactement comme avant.** Un `CharacterVirtual` de Jolt
heurte aussi les faces arrière, par défaut. Marcher dans le niveau ne prouve donc **rien**
sur l'orientation de ses faces.

C'est une leçon qui vaut au-delà de ce jalon : un test qui passe ne garde que ce qu'il
teste réellement, et la seule façon de savoir ce qu'il garde est de casser exprès ce qu'il
prétend surveiller. Sans cette contre-épreuve, j'aurais livré un test rassurant et vide.

## 2.3 Ce qui garde vraiment : la normale rapportée par un rayon

Le témoin correct, dans un test sans GPU, est un **rayon**. On en tire cinq depuis le
milieu du hall — vers les quatre murs, le plafond et le sol — et on vérifie deux choses :

```cpp
const physics::RayHit hit = world.raycast(middle, probe.direction, 12.0f);
REQUIRE(hit.hit);
CHECK(hit.distance == doctest::Approx(probe.distance).epsilon(0.02));
CHECK(glm::dot(hit.normal, probe.direction) < -0.99f);
```

La distance dit que la pièce est bien fermée, et à la bonne taille. La dernière ligne est
celle qui porte tout le poids : **la surface nous fait face, sa normale remonte le rayon.**

Sur une face retournée, le rayon touche quand même — mais il revient avec une normale
opposée. Or c'est exactement la quantité dont le rendu se sert pour éliminer les faces
arrière et pour éclairer. Une normale à l'envers, c'est un mur absent à l'écran.

Contre-épreuve refaite avec la sonde : les trois murs de plâtre du hall échouent
(`dot = +1` au lieu de `−1`), le sol et le plafond — qui sont en carrelage et en béton,
non retournés — passent. Le test mord exactement là où il faut.

Les cinq tests de `tests/scene/test_niveau.cpp` couvrent donc :

1. les noms cités par la scène existent tous (maillages, matières, collisions, pas) ;
2. les murs présentent leur face avant à la pièce — **la sonde ci-dessus** ;
3. le joueur tient debout sur le sol du niveau, sans le traverser ;
4. un mur l'arrête, à un rayon près ;
5. l'embrasure le laisse passer, sans dérive latérale.

Les tests 3 à 5 ne gardent pas l'enroulement, on vient de le voir. Ils gardent autre chose,
qui compte tout autant : que la collision existe, qu'elle soit au bon endroit, et que le
trou du linteau soit assez large pour un corps de 70 cm de diamètre.

---

# 3. Brique 3 — un modèle importé doit aussi collisionner

## 3.1 Le problème

`importModel` de M5.5 chargeait un glTF, en faisait un maillage GPU et des textures, puis
**laissait tomber la géométrie**. C'était sans conséquence pour Suzanne et le loquet, qui
ne sont que du décor.

Ça ne l'est plus pour un niveau : ses entités déclarent `"shape": "mesh"` et citent leur
géométrie de collision par son nom. Sans elle, la scène se charge, le niveau s'affiche —
et on le traverse de part en part.

## 3.2 La géométrie reste côté processeur

Un maillage GPU ne se relit pas. Une fois les sommets envoyés à la carte, les récupérer
demanderait un transfert en sens inverse, coûteux et à contretemps. La solution est donc
d'en garder une copie là où elle est déjà : en mémoire centrale, au moment du chargement.

```cpp
out.positions = meshData.positions;
out.indices = meshData.indices;
scene::CollisionMesh collision;
collision.positions = out.positions.data();
// ...
m_resources.addCollisionMesh(file.name, collision);
```

Trois points méritent d'être notés.

**La copie appartient au modèle, pas à la table.** `CollisionMesh` est une *vue* : elle ne
porte que des pointeurs, exactement comme la table ne porte qu'un pointeur vers le maillage
GPU. Les tableaux doivent donc vivre au moins aussi longtemps qu'elle — d'où leur place
dans `ImportedModel`, qui est membre de l'application.

**Positions et indices seulement.** Ni normales, ni UV, ni tangentes : la collision ne s'en
sert pas. C'est ce qui ramène la copie à un cinquième de la géométrie complète.

**Même nom logique pour les deux.** Le maillage d'affichage et la géométrie de collision
sont enregistrés sous `niveau_beton` tous les deux. Ce sont deux tables distinctes, donc
aucune ambiguïté — et une seule chose à écrire dans le fichier de scène.

## 3.3 Ce que ça coûte

**32,2 Kio** pour les quatre maillages du niveau. La règle s'applique désormais à *tout*
modèle importé, y compris Suzanne, qui n'en a aucun usage : c'est un gaspillage assumé,
parce que le seuil où il faudrait le rendre optionnel est très loin — il faudrait une
centaine de mégaoctets de modèles pour que la question se pose.

---

# 4. Ce qui marche / ce qui ne marche pas / ce qui vient après

**Ce qui marche.** Le jeu démarre dans le hall d'un vrai niveau. On parcourt cinq pièces
reliées par quatre embrasures, on entend ses pas changer selon qu'on foule le carrelage ou
les planches, quatre lampes chaudes et rares laissent le reste dans le noir. Le décor
s'arrête sous la main et sous les pieds. Le générateur est **déterministe** : relancé, il
réécrit les mêmes octets, ce qui rend toute modification lisible dans un diff.

**Ce qui ne marche pas.**

- Le niveau est **vide de mobilier**. Aucune caisse, aucune porte, aucune source sonore :
  les objets de la scène de démonstration n'y ont pas été transportés. La porte à
  charnière, la saisie d'objets et l'occlusion audio sont donc toujours visibles dans
  `demo.json`, mais pas dans `niveau.json`.
- **Aucun graphe de secteurs.** Le niveau est une seule zone : l'occlusion audio par les
  murs, qui repose sur le graphe de M3, ne s'y applique pas.
- **Les embrasures n'ont pas de porte** — seulement un trou. Leur largeur (1,20 m) et leur
  hauteur (2,10 m) sont pourtant faites pour en recevoir une.
- Le plan est **écrit dans le script**, pas dans un fichier de données. Changer le niveau
  demande de modifier du Python. C'est un choix assumé tant qu'il n'y a qu'un niveau.

**Ce qui vient après.** Meubler : reprendre la porte, les caisses et les sources sonores de
`demo.json` et les poser dans les pièces — ce que l'éditeur de M6 sait déjà faire, et
enregistrer. Puis découper le niveau en secteurs, ce qui redonnera son sens à l'occlusion
audio et servira de base au culling. Le reste appartient à M7.
