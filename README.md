# DAW IA

DAW assisté par IA. Socle natif Tracktion Engine + JUCE, services Python séparés, workspaces déclaratifs.

## Arborescence

```
core/                 C++20 — tout le natif
  domain/             logique métier pure (Command Bus, modèle, versioning). Jamais de JUCE ni de ui.
  engine/             adaptateur domain ↔ Tracktion Engine (audio, hébergement VST3/CLAP)
  ui/                 affichage uniquement
    tokens/           tokens.json — seule source des valeurs visuelles
  app/                point d'entrée, câblage domain + engine + ui
  tests/              tests du domaine (sans JUCE)
services/             Python — process séparé, JSON-RPC sur socket local
  src/daw_services/   rpc, mcp, harmony, conditioning, ia_provider
workspaces/           manifestes JSON déclaratifs
  schema/             JSON Schema des manifestes
  decouverte.json  beatmaker.json  ugc.json  film.json
external/             sous-modules épinglés : JUCE, tracktion_engine, clap, vst3sdk, BLAKE3
cmake/                modules CMake du projet (warnings, garde-fous, dépendances)
scripts/              installation, sous-modules, vérifications
```

Les trois modules ne partagent aucun code. Ils communiquent par contrats : JSON-RPC (core ↔ services) et JSON Schema (workspaces).

## Démarrage (Ubuntu 24.04)

```bash
./scripts/setup-ubuntu.sh
./scripts/bootstrap-submodules.sh
cmake --preset linux-clang
cmake --build --preset linux-clang
ctest --preset linux-clang
```

Domaine seul, sans sous-modules : `cmake --preset domain-only`.

## Démarrage (Windows 10/11)

Installation (Git, Python 3.12, uv, Build Tools VS 2022 avec MSVC + Windows SDK + CMake + Ninja, clang-format 18) :

```bat
powershell -ExecutionPolicy Bypass -File scripts\setup-windows.ps1
```

Puis, dans « x64 Native Tools Command Prompt for VS 2022 » :

```bat
git submodule update --init external/JUCE external/tracktion_engine external/clap external/vst3sdk external/BLAKE3
git -C external/vst3sdk submodule update --init base pluginterfaces public.sdk cmake
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

## Règles d'hygiène (vérifiées en CI)

1. **Aucune valeur visuelle en dur.** Couleurs, espacements, tailles, rayons, tailles de police : `core/ui/tokens/tokens.json` via `daw::ui::Tokens`.
2. **Aucun panneau ne connaît sa position ou sa taille.** Seuls les hôtes de layout (`*View`, `*Layout*`, `core/ui/layout/`, fenêtres) appellent `setBounds`.
3. **Logique métier séparée de l'affichage.** `daw_domain` ne lie ni JUCE ni Tracktion ni ui (échec au configure CMake) et n'inclut aucun de leurs en-têtes (`scripts/check_hygiene.py`).

Plus : Command Bus — toute mutation d'état passe par une commande sérialisable.

## Plugins de l'utilisateur (VST3 et CLAP)

`docs/plugins.md` décrit le modèle d'état, les commandes, le scan et l'hôte CLAP.
L'essentiel :

```bat
rem scanner les plugins installés, et persister la liste
"DAW IA.exe" --scan

rem jouer la démo avec un plugin réel, et ouvrir sa fenêtre
"DAW IA.exe" --demo --plugin "C:\Program Files\Common Files\VST3\MonSynthe.vst3"
```

Les tests qui touchent un vrai plugin portent le label `audio` et sont exclus de
la CI. Un plugin tiers se désigne par variable d'environnement — absente, le test
est sauté ; présente et sans son, le test est rouge :

```bat
set DAW_TEST_VST3=C:\Program Files\Common Files\VST3\MonSynthe.vst3
set DAW_TEST_CLAP=C:\Program Files\Common Files\CLAP\MonSynthe.clap
ctest --preset windows-msvc -L audio
```

Le dépôt compile aussi son propre plugin CLAP de test
(`core/engine/tests/clap_fixture/`) : un hôte ne se teste pas contre un mock, et
un plugin commercial ne se commite pas.

## Versions épinglées

| Sous-module | Version | Remarque |
|---|---|---|
| JUCE | `37c894f` (8.0.13, develop) | commit exact attendu par Tracktion (`modules/juce`) |
| tracktion_engine | `00fe427` (3.5.0, develop) | seul `modules/` est ajouté, pas son JUCE interne |
| clap | `1.2.10` | header-only ; l'hôte CLAP est écrit ici (`core/engine/src/clap/`) |
| vst3sdk | `v3.8.1_build_84` | `base`, `pluginterfaces`, `public.sdk`, `cmake` uniquement |
| BLAKE3 | `1.8.7` | digest des états de plugin ; SIMD épinglé sur la voie intrinsics |

Pour monter JUCE : prendre le commit que Tracktion référence dans `modules/juce`, jamais une version indépendante.
