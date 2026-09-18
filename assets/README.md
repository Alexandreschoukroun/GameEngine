# Données du jeu

Ce dossier contient les données chargées à l'exécution : modèles, textures, et plus tard
les shaders, les sons et les niveaux.

Le moteur le trouve dans cet ordre (voir `engine/platform/src/paths.cpp`) :

1. la variable d'environnement `GAMEENGINE_ASSETS`, si elle désigne un dossier existant ;
2. un dossier `assets/` à côté de l'exécutable — la disposition du jeu distribué ;
3. ce dossier-ci, dont le chemin est inscrit par CMake — la disposition de développement.

## Provenance et licences

| Fichier | Source | Licence |
|---|---|---|
| `models/suzanne/*` | [glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Suzanne), Khronos Group | **CC0 1.0** (domaine public) |

Le modèle Suzanne est le maillage de test historique de Blender. Il est ici parce qu'il
est en CC0, donc sans aucune obligation d'attribution ni de citation, et qu'il porte tout
ce dont le renderer aura besoin : normales, tangentes, coordonnées de texture, et des
textures PBR (couleur de base, métallicité/rugosité).

**Règle pour la suite** : toute donnée ajoutée ici doit avoir une licence compatible avec
une distribution commerciale, et être listée dans ce tableau. Construire six mois sur des
fichiers « gratuits pour un usage non commercial » est une erreur qui ne se rattrape pas.
