# GameEngine

[![Build](https://github.com/Alexandreschoukroun/GameEngine/actions/workflows/build.yml/badge.svg)](https://github.com/Alexandreschoukroun/GameEngine/actions/workflows/build.yml)

Un moteur de jeu **open source** en **C++20**, spécialisé dans les jeux d'**horreur first-person** sur PC, écrit de zéro.

Pas de moteur généraliste ici. Tout est pensé pour un seul genre : des intérieurs clos et sombres, une lampe torche, du brouillard, un son qui trahit ce qui se passe derrière une porte, et une créature qui vous traque.

> **État actuel : jalon M0 (socle).** Le moteur ouvre une fenêtre, fait tourner une boucle de jeu à pas fixe et efface l'écran. Il n'y a encore aucun rendu 3D. Voir la [feuille de route](#feuille-de-route).

## Objectifs

Le projet a deux objectifs, dans cet ordre :

1. **Apprendre.** C'est d'abord un projet d'apprentissage : chaque brique (rendu, audio, physique, IA) doit être comprise, pas seulement fonctionner. Chacune est documentée dans [`docs/`](docs/) : problème, options étudiées, choix retenu, vocabulaire, coût.
2. **Produire un moteur utilisable**, d'abord par son auteur, puis par un non-développeur, pour créer des jeux d'horreur first-person.

## Ce qui en fait un moteur « horreur »

- **Éclairage** : rendu deferred avec ombres sur chaque lumière. La lampe torche est un élément de premier plan (cookie de texture, batterie, tremblement, inertie). Le brouillard est volumétrique, avec god rays.
- **Audio, le système central** : spatialisation binaurale (HRTF), occlusion (un son derrière une porte fermée est étouffé), reverb par pièce, sons de pas qui dépendent du matériau, et une musique de tension pilotée en continu.
- **IA de l'antagoniste** : il voit (cône de vision) et entend. Le son se propage de pièce en pièce, pas à vol d'oiseau. Il garde en mémoire la dernière position connue du joueur. Un *AI Director* règle le rythme de la tension.
- **Interaction physique** : portes et tiroirs manipulés à la main, à la *Amnesia*, et saisie d'objets.
- **Séquences scriptées** en Lua : un scare doit tenir en une dizaine de lignes.
- **Accessibilité intégrée dès la conception** : sous-titres directionnels, remapping complet, réduction des mouvements, avertissement optionnel avant un jump scare.

### Hors périmètre

Open world, terrain, multijoueur, VR, consoles, mobile, 2D, ray tracing matériel, système de plugins tiers. Ces limites sont volontaires : c'est ce qui rend le projet faisable.

## Stack

| Domaine | Choix |
|---|---|
| Langage / build | C++20, CMake ≥ 3.25, vcpkg (manifest) |
| Fenêtre / input | SDL3 |
| Rendu | OpenGL 4.6 (Vulkan plus tard, derrière une abstraction GPU) |
| Physique | Jolt Physics |
| Audio | miniaudio + DSP maison |
| ECS / scène | EnTT, sérialisation JSON texte déterministe |
| Éditeur | Dear ImGui (docking) + ImGuizmo |
| Scripting | Lua 5.4 via sol2 |
| Assets | glTF 2.0 (cgltf), stb_image |
| Profiling / tests | Tracy, doctest |

Aujourd'hui, seules **SDL3** et **doctest** sont intégrées. Les autres arrivent avec leur jalon.

## Architecture

Le moteur est une **bibliothèque**, le jeu est l'**exécutable** qui la consomme. Les couches ne dépendent que de celles situées en dessous d'elles :

```
        game/          jeu de démonstration
        editor/        outils (ImGui)
   ──────────────────────────────────────────────
        runtime/       scène, physique, audio, IA, script, a11y
   ──────────────────────────────────────────────
        renderer/      passes, matériaux, lumières
   ──────────────────────────────────────────────
        rhi/           abstraction GPU (OpenGL)
   ──────────────────────────────────────────────
        platform/      SDL3 : fenêtre, input, temps
        core/          types, log, assert, maths
```

Quelques règles structurantes :
- aucun type d'une bibliothèque tierce ne sort de sa couche ;
- pas d'exceptions ni de RTTI ;
- zéro allocation dans la boucle de frame ;
- les scènes sont sérialisées en JSON trié et stable, donc lisibles et fusionnables dans Git.

Le détail est dans [`SPEC.md`](SPEC.md).

## Feuille de route

| Jalon | Contenu | État |
|---|---|---|
| **M0** | Socle : CMake + vcpkg, core, fenêtre SDL3, boucle à pas fixe | En cours |
| M1 | Abstraction GPU + backend OpenGL 4.6, triangle texturé, caméra libre | À venir |
| M2 | Renderer PBR deferred, glTF, lumières et ombres, lampe torche | À venir |
| M3 | Scène (EnTT), sérialisation JSON, graphe de secteurs/portails | À venir |
| M4 | Physique Jolt, character controller, portes, saisie d'objets | À venir |
| M5 | Audio : spatialisation, occlusion, reverb zones | À venir |
| M6 | Éditeur : viewport, hiérarchie, inspecteur, gizmos | À venir |
| M7 | Lua, triggers, navmesh, perception et IA de l'antagoniste | À venir |
| M8 | Brouillard volumétrique, post-process, accessibilité | À venir |
| M9 | Packaging et jeu de démonstration jouable de 15 minutes | À venir |

Un jalon n'est terminé que s'il compile sans warning, qu'il est documenté dans `docs/`, et que son fonctionnement peut être expliqué.

## Compiler

**Prérequis** (Windows) : Visual Studio 2022 ou plus récent, avec le workload « Développement Desktop en C++ » (il fournit CMake), et Git.

```powershell
# Une seule fois : vcpkg au commit épinglé dans vcpkg.json (champ builtin-baseline)
git clone https://github.com/microsoft/vcpkg vcpkg
git -C vcpkg checkout <builtin-baseline>
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

```powershell
# Depuis un « Developer PowerShell for VS 2022 »
$env:VCPKG_ROOT = "$PWD\vcpkg"
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

.\build\windows-msvc\game\Debug\game.exe    # Échap pour quitter
```

Le premier `cmake --preset` compile les dépendances, ce qui prend quelques minutes. Les suivants sont instantanés. La CI GitHub Actions exécute exactement ces commandes, en Debug et en Release, à chaque push. Détails dans [`docs/build-et-ci.md`](docs/build-et-ci.md).

## Structure du dépôt

```
├── SPEC.md          spécification : périmètre, stack, règles, jalons
├── docs/            une doc par brique, rédigée au fil des jalons
├── engine/          le moteur (bibliothèques)
│   ├── core/
│   └── platform/
├── game/            l'exécutable de démonstration
├── tests/           tests unitaires (doctest)
├── cmake/           modules CMake (warnings stricts)
└── .github/         pipeline de CI
```

## Documentation

- [`SPEC.md`](SPEC.md) : la source de vérité du projet
- [`docs/00-architecture.md`](docs/00-architecture.md) : le socle M0 (fenêtre, boucle à pas fixe)
- [`docs/build-et-ci.md`](docs/build-et-ci.md) : build, tests et intégration continue

## Licence

Le moteur est distribué sous licence [MIT](LICENSE). Il peut être utilisé, modifié et servir à créer des jeux, y compris commerciaux, à condition de conserver la mention de copyright.

Les bibliothèques tierces gardent leur propre licence, toutes permissives (MIT, zlib, BSD). Un jeu distribué doit inclure leurs mentions.
