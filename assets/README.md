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

Le modèle Suzanne est le maillage de test historique de Blender. Il est ici parce qu'il
est en CC0, donc sans aucune obligation d'attribution ni de citation, et qu'il porte tout
ce dont le renderer aura besoin : normales, tangentes, coordonnées de texture, et des
textures PBR (couleur de base, métallicité/rugosité).

## Les sons sont générés, pas téléchargés

Les fichiers audio sont produits par un script, `tools/generate_audio.py`, à partir de
bruit filtré et d'oscillateurs. Ce n'est pas un choix esthétique : c'est la seule façon
d'avoir des sons dont la provenance est **certaine**, sans dépendre d'une banque dont la
licence changera peut-être, et sans alourdir l'historique Git de fichiers binaires qu'on ne
pourrait pas regénérer.

Ils seront remplacés par de vrais enregistrements le jour où le moteur en vaudra la peine.
D'ici là, ils suffisent à ce qu'on leur demande : vérifier la spatialisation et l'occlusion.

**Règle pour la suite** : toute donnée ajoutée ici doit avoir une licence compatible avec
une distribution commerciale, et être listée dans ce tableau. Construire six mois sur des
fichiers « gratuits pour un usage non commercial » est une erreur qui ne se rattrape pas.
