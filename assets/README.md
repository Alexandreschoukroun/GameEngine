# Données du jeu

Ce dossier contient les données chargées à l'exécution : modèles, textures, shaders, sons
et niveaux.

Le moteur le trouve dans cet ordre (voir `engine/platform/src/paths.cpp`) :

1. la variable d'environnement `GAMEENGINE_ASSETS`, si elle désigne un dossier existant ;
2. un dossier `assets/` à côté de l'exécutable — la disposition du jeu distribué ;
3. ce dossier-ci, dont le chemin est inscrit par CMake — la disposition de développement.

## Provenance et licences

| Fichier | Source | Licence |
|---|---|---|
| `models/suzanne/*` | [glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Suzanne), Khronos Group | **CC0 1.0** (domaine public) |
| `audio/braises.wav` | Généré par `tools/generate_audio.py` | Aucune — produit par ce dépôt |
| `audio/souffle.wav` | Généré par `tools/generate_audio.py` | Aucune — produit par ce dépôt |
| `audio/pas_pierre.wav` | Généré par `tools/generate_audio.py` | Aucune — produit par ce dépôt |
| `audio/pas_bois.wav` | Généré par `tools/generate_audio.py` | Aucune — produit par ce dépôt |
| `audio/tension_*.wav` | Généré par `tools/generate_audio.py` | Aucune — produit par ce dépôt |
| `textures/pierre_*.png` | Généré par `tools/generate_textures.py` | Aucune — produit par ce dépôt |
| `textures/bois_*.png` | Généré par `tools/generate_textures.py` | Aucune — produit par ce dépôt |
| `textures/beton/*` | [ambientCG — Concrete032](https://ambientcg.com/view?id=Concrete032) | **CC0 1.0** |
| `textures/plancher/*` | [ambientCG — Planks039](https://ambientcg.com/view?id=Planks039) | **CC0 1.0** |
| `textures/platre/*` | [ambientCG — PaintedPlaster016](https://ambientcg.com/view?id=PaintedPlaster016) | **CC0 1.0** |
| `textures/metal_rouille/*` | [ambientCG — Metal041B](https://ambientcg.com/view?id=Metal041B) | **CC0 1.0** |
| `textures/carrelage/*` | [ambientCG — Tiles140](https://ambientcg.com/view?id=Tiles140) | **CC0 1.0** |

Le modèle Suzanne est le maillage de test historique de Blender. Il est ici parce qu'il
est en CC0, donc sans aucune obligation d'attribution ni de citation, et qu'il porte tout
ce dont le renderer aura besoin : normales, tangentes, coordonnées de texture, et des
textures PBR (couleur de base, métallicité/rugosité).

## Les sons et les textures sont générés, pas téléchargés

Les fichiers audio et les textures sont produits par deux scripts, `tools/generate_audio.py`
et `tools/generate_textures.py`, à partir de bruit filtré, d'oscillateurs et de champs de
hauteur. Ce n'est pas un choix esthétique : c'est la seule façon
d'avoir des données dont la provenance est **certaine**, sans dépendre d'une banque dont la
licence changera peut-être, et sans alourdir l'historique Git de fichiers binaires qu'on ne
pourrait pas regénérer.

Ils seront remplacés par de vrais assets le jour où le moteur en vaudra la peine. D'ici là,
ils suffisent à ce qu'on leur demande : vérifier la spatialisation, l'occlusion et le relief.

## Les matières libres présentes

Cinq matières d'ambientCG, en **1024 × 1024**, converties au format du moteur. La licence
CC0 1.0 est confirmée par [la page de licence du site](https://ambientcg.com/license) :
copie, modification et distribution libres, **usage commercial inclus**, sans attribution
obligatoire — le tableau ci-dessus la mentionne quand même, parce que savoir d'où vient une
donnée a de la valeur indépendamment de ce que la licence exige.

Chaque dossier contient exactement trois fichiers :

| Fichier | Format | Pourquoi |
|---|---|---|
| `couleur.jpg` | JPEG qualité 92 | une couleur : la compression ne s'y voit pas |
| `normal.png` | PNG sans perte | des **directions** : un artefact déformerait le relief |
| `matiere.png` | PNG sans perte | des **mesures** : R = occlusion, G = rugosité, B = métallicité |

**Ce qui n'est pas versionné** : les archives d'origine, les cartes de déplacement, les
variantes DirectX des normales, et l'occlusion ambiante. Cette dernière mérite une
explication — le shader ne lit aujourd'hui que le vert et le bleu de `matiere.png`, donc
l'empaqueter n'ajouterait qu'un canal de bruit. Or le bruit ne se compresse pas : cela
coûtait **1,5 Mo par matière**, définitivement, pour une donnée inutilisée. Le jour où le
moteur exploitera l'occlusion, il suffira de relancer `pack_material` avec `--ao`.

Le choix du **1K** est délibéré. Une surface vue à plus de deux mètres ne montre pas la
différence avec du 2K, et Git conserve **chaque version d'un binaire pour toujours**, sans
compression delta. Un 4K remplacé trois fois, ce sont 200 Mo que plus personne ne pourra
enlever de l'historique.

## Importer un jeu de textures libre

Les banques CC0 — [ambientCG](https://ambientcg.com), [Poly Haven](https://polyhaven.com) —
livrent une matière en plusieurs fichiers séparés. Le moteur en attend trois, et deux
conversions sont nécessaires.

**1. La couleur de base** se prend telle quelle (`_Color`, `diff`). Elle est en sRGB.

**2. La carte de normales : prenez la variante OpenGL.** ambientCG la nomme `NormalGL`,
Poly Haven `nor_gl`. Ce n'est pas un détail de nommage — **le canal vert est inversé**
entre les conventions OpenGL et DirectX. Prendre la version DX donne un relief creusé là
où il devrait ressortir, et l'erreur est presque invisible sans comparaison côte à côte.

**3. La rugosité et la métallicité doivent être réunies en une seule image.** Les banques
les livrent en deux fichiers de niveaux de gris ; le format glTF les veut dans un seul,
chaque grandeur dans son canal. C'est le rôle de l'outil `pack_material` :

```bash
# Une matière métallique, avec sa carte de métallicité
pack_material assets/textures/rouille_matiere.png \
    --roughness Rust004_Roughness.png \
    --metalness Rust004_Metalness.png

# Le cas courant : une matière non métallique n'a pas de carte du tout
pack_material assets/textures/beton_matiere.png \
    --roughness Concrete030_Roughness.png \
    --metalness 0
```

L'outil accepte une **valeur** de 0 à 1 à la place d'un fichier, et refuse des entrées de
tailles différentes plutôt que de produire une carte où les canaux ne décrivent pas le
même point de la surface.

Il reste ensuite à déclarer la matière dans le jeu et à la citer dans la scène :

```json
"mesh": { "mesh": "piece", "material": "beton" }
```

**Un piège d'échelle** : une texture libre est faite pour se **répéter**. Les coordonnées
de texture du moteur sont exprimées en mètres, pas de 0 à 1 — un mur de 8 m reçoit des UV
allant jusqu'à 4 si la texture couvre 2 m. Sans cela, le béton serait étiré sur toute la
paroi et paraîtrait flou et gigantesque.

**Règle pour la suite** : toute donnée ajoutée ici doit avoir une licence compatible avec
une distribution commerciale, et être listée dans ce tableau. Construire six mois sur des
fichiers « gratuits pour un usage non commercial » est une erreur qui ne se rattrape pas.
