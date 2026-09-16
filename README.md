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
external/             sous-modules épinglés : JUCE, tracktion_engine, clap, vst3sdk
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

Windows : ouvrir un « x64 Native Tools Command Prompt for VS 2022 », puis presets `windows-msvc`.

Services :

```bash
cd services && uv sync && uv run pytest
```

## Règles d'hygiène (vérifiées en CI)

1. **Aucune valeur visuelle en dur.** Couleurs, espacements, tailles, rayons, tailles de police : `core/ui/tokens/tokens.json` via `daw::ui::Tokens`.
2. **Aucun panneau ne connaît sa position ou sa taille.** Seuls les hôtes de layout (`*View`, `*Layout*`, `core/ui/layout/`, fenêtres) appellent `setBounds`.
3. **Logique métier séparée de l'affichage.** `daw_domain` ne lie ni JUCE ni Tracktion ni ui (échec au configure CMake) et n'inclut aucun de leurs en-têtes (`scripts/check_hygiene.py`).

Plus : Command Bus — toute mutation d'état passe par une commande sérialisable.

## Versions épinglées

| Sous-module | Version | Remarque |
|---|---|---|
| JUCE | `37c894f` (8.0.13, develop) | commit exact attendu par Tracktion (`modules/juce`) |
| tracktion_engine | `00fe427` (3.5.0, develop) | seul `modules/` est ajouté, pas son JUCE interne |
| clap | `1.2.10` | header-only |
| vst3sdk | `v3.8.1_build_84` | `base`, `pluginterfaces`, `public.sdk`, `cmake` uniquement |

Pour monter JUCE : prendre le commit que Tracktion référence dans `modules/juce`, jamais une version indépendante.
