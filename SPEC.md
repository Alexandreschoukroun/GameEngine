# SPEC — Moteur de jeu spécialisé horreur (PC, C++20, open source)

> Ce fichier est la source de vérité du projet. Relis-le au début de chaque session.
> Si une décision de ce document te paraît mauvaise, **dis-le explicitement avant de coder**, ne l'applique pas en silence.

---

## 1. Contexte

Je suis développeur C++ (temps réel embarqué) et fullstack web. Je n'ai jamais écrit de moteur de jeu ni fait de 3D.
Machine de dev : Windows, RTX 4070 Super.

Ce projet a **deux** objectifs, dans cet ordre :

1. **Que je comprenne réellement chaque brique.** C'est un projet d'apprentissage avant tout. Du code qui marche mais que je ne sais pas expliquer est un échec.
2. Produire un moteur utilisable par moi, puis par un non-développeur, pour faire des jeux d'horreur first-person sur PC.

---

## 2. Périmètre

### Le moteur fait ça, et rien d'autre

- Jeux **first-person**, **solo**, **PC** (Windows en priorité, Linux ensuite)
- **Intérieurs clos** : couloirs, pièces, sous-sols. Portée de vue courte (< 50 m)
- Ambiance sombre : peu de sources de lumière, mais ombres et brouillard de haute qualité
- Audio spatialisé de qualité, avec occlusion — c'est le **système central**, pas un accessoire
- Interaction physique à la première personne (portes, tiroirs, objets à saisir)
- Un antagoniste IA qui perçoit et traque le joueur
- Séquences scriptées (scares, événements, déclencheurs)
- Accessibilité intégrée dès le départ, pas ajoutée à la fin

### Non-goals — à refuser si je les demande par accident

Open world, terrain, végétation, multijoueur/réseau, véhicules, 2D, VR, consoles, animation faciale, ragdoll complexe, système de plugins tiers, éditeur de shaders visuel, mobile, ray tracing matériel.

---

## 3. Stack verrouillée

Aucune de ces dépendances n'est à réécrire. Si tu penses qu'il en faut une autre, argumente d'abord.

| Domaine | Choix | Raison |
|---|---|---|
| Langage | C++20 | modules non utilisés, headers classiques |
| Build | CMake ≥ 3.25 + vcpkg (manifest mode) | reproductible |
| Fenêtre / input | SDL3 | gère aussi manettes et clavier |
| Rendu | OpenGL 4.6 (Direct State Access) | API Vulkan prévue plus tard, derrière la RHI |
| Physique | Jolt Physics | déterministe, moderne, utilisé en AAA |
| Audio | miniaudio (bas niveau) + DSP maison | l'audio est notre différenciateur, on ne prend pas FMOD |
| ECS | EnTT | |
| UI éditeur | Dear ImGui (docking branch) + ImGuizmo | |
| Maths | GLM | |
| Import 3D | cgltf | **glTF 2.0 uniquement**, aucun autre format |
| Images | stb_image + KTX2/Basis plus tard | |
| Scripting | Lua 5.4 via sol2 | |
| Sérialisation | JSON (nlohmann) en **texte lisible** | mergeable dans Git — non négociable |
| Profiling | Tracy | |
| Tests | doctest | |

---

## 4. Règles d'architecture — non négociables

1. **Couches strictes, dépendances à sens unique.** Une couche ne connaît que celles en dessous d'elle. Jamais l'inverse, jamais de cycle.

```
        game/          (le jeu démo)
        editor/        (ImGui, outils)
   ─────────────────────────────────────
        runtime/       scene, physics, audio, ai, script, a11y
   ─────────────────────────────────────
        renderer/      passes, matériaux, lumières
   ─────────────────────────────────────
        rhi/           abstraction GPU (OpenGL aujourd'hui)
   ─────────────────────────────────────
        platform/      SDL3 : fenêtre, input, fichiers, temps
        core/          types, log, assert, allocateurs, maths, jobs
```

2. **Aucune dépendance tierce ne fuit au-dessus de sa couche.** Pas de type Jolt dans `renderer/`, pas de type OpenGL au-dessus de `rhi/`, pas de `SDL_Event` au-dessus de `platform/`. On wrappe.
3. **Sérialisation déterministe dès le jour 1.** IDs stables (UUID 64 bits), clés triées, sortie identique pour une même scène. C'est le fix du problème de merge des assets binaires, impossible à rattraper plus tard.
4. **Le moteur est une bibliothèque, le jeu est l'exécutable.** `game/` consomme le moteur, jamais l'inverse. Rien de spécifique à mon jeu ne remonte dans `engine/`.
5. **On n'abstrait pas avant d'avoir deux implémentations concrètes.** Pas d'interface « au cas où ».
6. **Pas d'exceptions, pas de RTTI.** Codes d'erreur explicites, `std::expected` si utile. *Exception assumée : des libs tierces de la stack (nlohmann::json, sol2/Lua) lèvent des exceptions en interne. On ne les interdit pas dans ces libs, mais on catch systématiquement à la frontière du wrapper (couche qui les encapsule) et on convertit en code d'erreur/`std::expected` avant de remonter dans le moteur. Tranché en détail à M3 (sérialisation) et M7 (script).*
7. **Zéro allocation dans la boucle de frame** une fois le socle posé. Arena/pool allocators dans `core/`.

---

## 5. Arborescence

```
<engine-name>/
├── CMakeLists.txt
├── vcpkg.json
├── SPEC.md                 ← ce fichier
├── docs/                   ← un .md par brique, écrit au fur et à mesure
│   ├── 00-architecture.md
│   ├── 01-rhi.md
│   └── ...
├── engine/
│   ├── core/
│   ├── platform/
│   ├── rhi/
│   ├── renderer/
│   ├── scene/
│   ├── physics/
│   ├── audio/
│   ├── ai/
│   ├── script/
│   ├── a11y/
│   └── assets/
├── editor/
├── game/
├── tools/
├── tests/
└── assets/                 ← contenu du jeu démo
```

---

## 6. Ce qui rend ce moteur spécifiquement « horreur »

C'est la partie qui justifie l'existence du projet. À ne pas traiter comme un bonus.

### 6.1 Éclairage
- Rendu **deferred**, budget assumé : ~8 lumières dynamiques visibles simultanément, toutes avec ombres
- **Lampe torche** comme citoyen de première classe : spot light avec cookie de texture, atténuation IES, batterie, tremblement, inertie de retard sur la caméra
- **Brouillard volumétrique** (raymarching en compute) avec god rays — l'effet signature du genre
- Pièces découpées en **volumes/secteurs** avec culling par portails : c'est ce qui rend possible des ombres de haute qualité sur un budget modeste. *Le graphe de secteurs/portails est construit à M3 (Scène), pas ici — M2 travaille sur une scène de test sans découpage, le culling par portails arrive quand le graphe existe.*

### 6.2 Audio — le système le plus important
- Spatialisation HRTF binaurale (casque prioritaire)
- **Occlusion et obstruction** : un son derrière une porte fermée doit être filtré passe-bas et atténué. Calcul par raycast + secteurs (réutilise le graphe de secteurs posé à M3)
- **Reverb zones** par pièce, avec interpolation lors des transitions
- Système de matériaux de surface : les pas, les impacts et les traînées d'objets dépendent du matériau touché
- **Couche de tension** : musique/drone paramétrique piloté par une variable `tension` 0..1, pas des morceaux fixes
- Budget : 64 voix simultanées

### 6.3 Perception et IA de l'antagoniste
- Navmesh **fait à la main au début** (mesh de navigation simplifié posé manuellement dans l'éditeur/scène). Recast/Detour n'est **pas** dans la stack verrouillée (section 3) : on ne l'ajoute que si on l'atteint à M7 et qu'on en a un besoin concret, avec validation explicite avant de l'ajouter (règle 210)
- **Modèle de perception** : cône de vision avec raycasts, ouïe avec propagation du son **par le graphe des pièces** et non à vol d'oiseau (un bruit derrière un mur porte moins loin) — même graphe de secteurs que 6.1/6.2, posé à M3
- Mémoire : dernière position connue du joueur, recherche, patrouille, désengagement
- **AI Director** : une entité qui pilote la tension globale (calme → montée → confrontation → répit), à la manière de Left 4 Dead
- ⚠️ Pour l'antagoniste « qui apprend », on commence par un **arbre de comportement + heuristiques sur l'historique du joueur** (zones où il se cache, rythme, routes préférées). Pas de machine learning avant que tout le reste fonctionne. Un BT bien réglé paraît plus intelligent qu'un modèle entraîné, et se débogue.

### 6.4 Interaction first-person
- Contrôleur de personnage sur Jolt (capsule, kinematic)
- Saisie d'objets physique et **manipulation de portes à la Amnesia** (on tire la poignée, on ne joue pas une animation)
- Inventaire simple, clés, objets utilisables
- Head bob, respiration, vitesse de déplacement liée à l'état

### 6.5 Post-process d'ambiance
Grain de film, aberration chromatique, vignette, distorsion « sanité », motion blur — tous paramétrables **et tous désactivables individuellement** (voir accessibilité).

### 6.6 Événements scriptés
Triggers volumiques, timelines d'événements, tout exposé en Lua. Un scare doit être scriptable par un non-développeur en 10 lignes.

### 6.7 Accessibilité — intégrée, pas optionnelle
L'horreur est le genre où ça compte le plus : jump scares, head bob, obscurité, indices sonores. Système `a11y/` disponible dès le jalon runtime :
- Sous-titres complets : taille, fond opaque, nom du locuteur, **indicateur directionnel** pour les sons hors champ
- Remapping complet clavier + manette, avec détection de conflits
- Réduction des mouvements : head bob, FOV, camera shake, motion blur — chacun désactivable
- Presets daltonisme et contraste
- **Avertissement de jump scare** optionnel (signal discret quelques secondes avant un trigger marqué comme tel)
- Navigation des menus au clavier + hooks pour lecteur d'écran

---

## 7. Jalons

Un jalon n'est terminé que si : ça compile sans warning, il y a un doc dans `docs/`, et je peux expliquer comment ça marche.

- **M0 — Socle** : CMake + vcpkg, `core/` (log, assert, types, maths, temps), `platform/` (fenêtre SDL3, boucle de jeu à pas fixe, input), écran effacé en couleur. *Livrable : une fenêtre qui s'ouvre et se ferme proprement.*
- **M1 — RHI + premier triangle** : abstraction GPU minimale (buffers, textures, shaders, pipelines, passes), backend OpenGL 4.6. *Livrable : triangle texturé, caméra libre.*
- **M2 — Renderer PBR** : chargement glTF, G-buffer deferred, lumières ponctuelles/spot, ombres (shadow maps), tonemapping. *Livrable : une pièce éclairée par une lampe torche.*
- **M3 — Scène** : EnTT, hiérarchie de transforms, sérialisation JSON déterministe, chargement/sauvegarde de niveau, **graphe de secteurs/portails** (fondation réutilisée par le culling M2+, l'occlusion audio M5 et l'ouïe de l'IA M7).
- **M4 — Physique + contrôleur** : Jolt, colliders depuis glTF, character controller, saisie d'objets, portes.
- **M5 — Audio** : miniaudio, spatialisation, occlusion par raycast, reverb zones, matériaux de pas.
- **M6 — Éditeur** : ImGui docking, viewport, hiérarchie, inspecteur, gizmos, play-in-editor, browser d'assets.
- **M7 — Script + IA** : Lua, triggers, navmesh, perception, arbre de comportement de l'antagoniste, AI Director.
- **M8 — Ambiance + a11y** : brouillard volumétrique, post-process, système de sous-titres, remapping, options d'accessibilité.
- **M9 — Packaging + démo** : build release, packaging d'assets, **jeu jouable de 15 minutes**.

---

## 8. Comment je veux que tu travailles

C'est la partie la plus importante du document.

### Pédagogie obligatoire
Avant d'écrire le code d'une brique nouvelle, tu m'expliques **dans le chat** :
1. **Le problème** que cette brique résout, concrètement
2. **Les options** existantes (2-3), avec leurs compromis
3. **Ton choix** et pourquoi, dans notre contexte horreur/PC
4. **Le vocabulaire** que je dois connaître (G-buffer, PSO, HRTF, navmesh...) — défini simplement
5. **Le coût** : mémoire, temps GPU/CPU attendu

Puis tu codes. Puis tu écris `docs/NN-brique.md` avec ce même contenu, en plus détaillé, plus un schéma ASCII du flux de données.

### Rythme
- **Un sous-système à la fois.** Jamais deux jalons dans la même session.
- Des commits atomiques avec un message qui explique le *pourquoi*.
- À chaque fin de tâche : ce qui marche, ce qui ne marche pas, ce qui vient après.

### Interdictions
- Ne génère pas plus de ~400 lignes de code par tour. Je dois pouvoir tout lire.
- Pas de code « pour plus tard », pas de TODO fantôme, pas d'interface sans implémentation.
- Ne réécris pas une lib de la stack.
- Pas de dépendance non listée en section 3 sans me demander.
- Si je te demande quelque chose qui contredit ce document (un non-goal, une abstraction prématurée, un raccourci sur la sérialisation), **refuse et rappelle-moi pourquoi**.

### Quand tu as un doute
Pose la question. Je préfère 3 questions à 300 lignes à jeter.

---

## 9. Budget de performance (cible)

Cible : 1080p, 60 fps minimum sur une GTX 1060 — donc 16,6 ms par frame, dont :

| Poste | Budget |
|---|---|
| G-buffer | 2,5 ms |
| Ombres (8 lumières) | 3,5 ms |
| Éclairage + volumétrique | 3,5 ms |
| Post-process | 1,5 ms |
| CPU (logique, physique, audio, IA) | 3,5 ms |
| **Marge (driver, swap, imprévu)** | **~2 ms** |

*Révisé : la version initiale totalisait 16,5 ms sur un budget de 16,6 ms — aucune marge. On vise 14,5 ms de dépense réelle et on garde ~2 ms de coussin explicite.*

Tracy branché dès M1. Toute régression au-delà du budget se traite immédiatement, pas « plus tard ».

---

## 10. Première tâche

Ne code pas encore.

1. Relis ce document et **dis-moi ce qui te paraît irréaliste, contradictoire ou mal ordonné**. Sois direct.
2. Propose-moi 3 noms pour le moteur.
3. Donne-moi le plan précis de **M0 uniquement** : fichiers à créer, rôle de chacun, ordre d'implémentation, et ce que j'aurai à l'écran à la fin.

Ensuite seulement, on attaque M0.
