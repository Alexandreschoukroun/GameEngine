# Build, tests et CI

## 1. Le problème

Le SPEC pose une règle de fin de jalon : « ça compile sans warning ». Vérifiée à la main, cette règle finit par ne plus l'être. Deux problèmes concrets :

1. **« Ça compile chez moi. »** Un build local dépend de ce qui est installé sur la machine. Rien ne garantit qu'un commit compile sur une machine propre, ni que les tests passent encore.
2. **Des dépendances non épinglées.** Sans version figée dans `vcpkg.json`, `sdl3` désigne « la version du clone vcpkg de la machine ». Deux machines peuvent donc compiler deux SDL3 différentes. Ça contredit le « reproductible » de la stack (section 3 du SPEC).

La pipeline de CI répond au premier problème : à chaque push et chaque pull request, une machine vierge configure, compile en Debug et en Release, puis lance les tests. La `builtin-baseline` répond au second.

## 2. Les options considérées

### Service de CI
- **GitHub Actions** : intégré au dépôt, gratuit pour un dépôt public sur les runners standards, le statut s'affiche directement sur les commits et les PR.
- **Service externe** (Azure Pipelines, GitLab CI, Jenkins) : un compte ou un serveur de plus à gérer, pour aucun gain ici.

**Choix : GitHub Actions**, puisque le dépôt est déjà sur GitHub.

### Obtenir vcpkg sur la machine de CI
- **Le vcpkg préinstallé sur l'image** : sa version dépend de la date de l'image. Si l'image est plus ancienne que notre baseline, le build casse.
- **Une action tierce** (`lukka/run-vcpkg`) : pratique, mais c'est une dépendance hors stack, à auditer et à maintenir.
- **Cloner vcpkg au commit de la baseline** : déterministe, aucune dépendance tierce, et c'est exactement ce qu'on fait en local.

**Choix : le clone à la baseline.** Le commit est lu dans `vcpkg.json`, il n'y a donc qu'une seule source de vérité.

### Où écrire les options CMake
- **Directement dans le YAML** : la CI et le build local finissent par diverger.
- **`CMakePresets.json`** : un fichier versionné. La même commande sert en local, dans Visual Studio (qui lit ce fichier nativement) et en CI.

**Choix : les presets.**

### Générateur CMake
- **Imposer `Visual Studio 17 2022`** : c'est explicite, mais ça casse dès que cette version exacte est absente. Ça s'est produit dès le premier run de CI : l'image `windows-2025` de GitHub embarque Visual Studio 2026, et la configuration a échoué avec « could not find any instance of Visual Studio ».
- **Ninja + environnement MSVC** : ne dépend pas de la version de Visual Studio, mais oblige à charger l'environnement du compilateur (`vcvars`) avant chaque commande, en local comme en CI. C'est une étape de plus à comprendre et à maintenir.
- **Ne pas préciser de générateur** : sur Windows, CMake choisit alors le Visual Studio le plus récent installé.

**Choix : ne pas préciser de générateur.** Conséquence à connaître : la CI compile avec un MSVC plus récent que ta machine (VS 2026 contre VS 2022). Si un nouveau compilateur ajoute un warning, la CI échouera (`/WX`) avant que tu le voies en local. C'est plutôt un avantage : tu es prévenu tôt.

### Plateformes
**Windows/MSVC seulement pour l'instant**, puisque le SPEC donne la priorité à Windows. Un job Linux s'ajoutera quand Linux deviendra une cible réelle : SDL3 y demande des paquets système (X11, Wayland), et les maintenir sans en avoir besoin serait du « pour plus tard ».

## 3. Vocabulaire

- **CI (intégration continue)** : compiler et tester automatiquement chaque changement poussé.
- **Workflow / job / step / runner** : un *workflow* (`.github/workflows/build.yml`) contient des *jobs*. Chaque job tourne sur une machine virtuelle neuve, le *runner*, et exécute ses *steps* dans l'ordre.
- **Matrice** : un même job lancé avec plusieurs jeux de paramètres. Ici `config: [debug, release]` produit deux jobs en parallèle.
- **builtin-baseline** : un commit du dépôt vcpkg. Il fige la version de chaque port (SDL3, doctest…). Même baseline, mêmes versions, sur toutes les machines.
- **Cache binaire vcpkg** : les dépendances déjà compilées sont archivées. Tant que rien ne change (version, compilateur, options), vcpkg les réutilise au lieu de les recompiler.
- **Générateur** : le type de projet que CMake produit à partir des `CMakeLists.txt`. Ce peut être une solution Visual Studio (`.sln` + `.vcxproj`, compilée par MSBuild) ou des fichiers Ninja. CMake décrit le projet, le générateur décide qui le compile.
- **Image du runner** : le système préinstallé sur la machine de CI (`windows-2025`). Le label fige la version de Windows. GitHub met en revanche à jour les outils (Visual Studio, CMake) chaque semaine environ.
- **Preset CMake** : un nom (`windows-msvc`) associé à un ensemble d'options (générateur, toolchain vcpkg, dossier de build).
- **Multi-config** : le générateur Visual Studio configure une seule fois, puis compile Debug ou Release au moment du build. C'est pour ça que la configuration (`windows-msvc`) n'a qu'un preset, alors que le build et les tests en ont deux.

## 4. Comment fonctionnent les tests

Deux outils s'empilent. Ils ne font pas le même travail.

**doctest** (le framework, dans le code) :
- `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` demande à doctest de générer la fonction `main()` de l'exécutable `engine_tests`.
- Chaque `TEST_CASE("...")` s'enregistre tout seul au démarrage, sans liste à tenir à jour.
- `CHECK(expr)` note un échec et continue le test. `REQUIRE(expr)` note un échec et arrête le test en cours.
- En fin d'exécution, le `main()` généré renvoie un code non nul si au moins un check a échoué.

**CTest** (le lanceur, côté CMake) :
- `add_test(NAME engine_tests COMMAND engine_tests)` déclare : « lance cet exécutable, et considère le test réussi si le code de retour vaut 0 ».
- CTest ne connaît pas les `TEST_CASE` individuels, seulement l'exécutable. Le détail des checks échoués vient de la sortie de doctest, que le preset affiche en cas d'échec (`outputOnFailure`).
- `noTestsAction: error` fait échouer la CI si aucun test n'est trouvé, par exemple si `GAMEENGINE_BUILD_TESTS` était désactivé par erreur. Sinon, « 0 test lancé » passerait pour un succès.

**Ce qui est testé en CI, et ce qui ne l'est pas** : `engine_tests` ne lie que `engine::core`, sans fenêtre ni GPU, donc il tourne sur un runner sans écran. L'exécutable `game` est **compilé** (warnings vérifiés) mais **jamais lancé** : le runner n'a pas de carte graphique.

**Ajouter un fichier de test** : `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` ne doit apparaître que dans **un seul** `.cpp`. Pour un deuxième fichier (`test_log.cpp`, par exemple), on déplace ce `#define` dans un `tests/main.cpp` dédié, puis on ajoute les deux fichiers à `add_executable(engine_tests ...)`.

## 5. Coût

- **Premier run** : clone de vcpkg (environ 1 min), puis compilation de SDL3 en Debug et en Release (quelques minutes), puis le moteur (quelques secondes).
- **Runs suivants** : les dépendances viennent du cache, seul le moteur est recompilé, soit quelques minutes au total, surtout à cause du clone et du démarrage de la machine.
- **Minutes GitHub** : gratuites sur les runners standards tant que le dépôt est public. S'il devient privé, les minutes sont décomptées d'un quota mensuel, et Windows coûte plus cher que Linux.
- **Invalidation du cache** : la clé dépend du hash de `vcpkg.json`. Changer de baseline ou de dépendance crée un nouveau cache. Une mise à jour du compilateur sur l'image ne change pas la clé : vcpkg recompile alors ce qui a changé, sans erreur, mais plus lentement.

## 6. Flux de données de la pipeline

```
  git push / pull request
            │
            ▼
  ┌──────────────────────────────────────────────────────────┐
  │ runner windows-2025 (VM neuve)       ×2 : debug | release │
  │                                                          │
  │  checkout du dépôt                                       │
  │       │                                                  │
  │       ▼                                                  │
  │  clone vcpkg ──► checkout builtin-baseline ──► bootstrap │
  │       │                          (lue dans vcpkg.json)   │
  │       ▼                                                  │
  │  restauration du cache binaire ◄──── cache GitHub        │
  │       │                                                  │
  │       ▼                                                  │
  │  cmake --preset windows-msvc                             │
  │       │   vcpkg installe sdl3 + doctest                  │
  │       │   (depuis le cache, sinon compilation)           │
  │       ▼                                                  │
  │  sauvegarde du cache ────────────────► cache GitHub      │
  │       │   (seulement si le cache était absent)           │
  │       ▼                                                  │
  │  cmake --build --preset windows-msvc-<config>            │
  │       │   /W4 /WX : un seul warning = build échoué       │
  │       ▼                                                  │
  │  ctest --preset windows-msvc-<config>                    │
  │       │   lance engine_tests, code retour 0 = succès     │
  └───────┼──────────────────────────────────────────────────┘
          ▼
  ✔ / ✘ affiché sur le commit et la PR
```

## 7. Reproduire la CI en local

Une seule fois :

```powershell
git clone https://github.com/microsoft/vcpkg vcpkg          # dossier ignoré par Git
git -C vcpkg checkout <valeur de builtin-baseline dans vcpkg.json>
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

Puis, depuis un **Developer PowerShell for VS 2022** (il met `cmake` et `ctest` dans le PATH) :

```powershell
$env:VCPKG_ROOT = "$PWD\vcpkg"
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Visual Studio (« Ouvrir un dossier ») détecte aussi `CMakePresets.json` et propose les mêmes presets. Il faut simplement que `VCPKG_ROOT` soit défini comme variable d'environnement.

**Mettre à jour les dépendances** : `git -C vcpkg pull`, puis `.\vcpkg\vcpkg.exe x-update-baseline`. Cette commande réécrit `builtin-baseline` dans `vcpkg.json`. On commite ce changement seul, avec le pourquoi dans le message.
