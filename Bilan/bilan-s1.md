# Bilan de fin de S1 — DAW IA

**Période :** semaine 1 sur 26 (15–21 septembre 2026). Rédigé le 18 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 4 commits (`a5bcb31` → `92aa218`).
**Local :** `C:\Users\User\Desktop\DAW IA`.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Script d'installation Ubuntu | Livré, partiellement vérifié | `scripts/setup-ubuntu.sh`, exécuté en CI en mode `--ci` |
| 2 | Arborescence à trois modules séparés | Livré | `core/` (C++), `services/` (Python), `workspaces/` (manifestes) |
| 3 | Sous-modules git | Livré | JUCE, Tracktion Engine, CLAP, VST3 SDK épinglés |
| 4 | CMake racine | Livré, vérifié | Build Windows en local, build Linux en CI |
| 5 | CI GitHub Actions, matrice Linux + Windows | Livré, vert | Run #2, 5 jobs, 5 min 19 s |
| 6 | clang-format | Livré, vérifié | `.clang-format` (clang-format 18), vérifié en CI |

Ajouts non demandés, faits en cours de semaine : `scripts/setup-windows.ps1`, `.editorconfig`, `.gitattributes`, vérification automatique des trois règles d'hygiène, validation des manifestes par JSON Schema.

## 2. Ce qui existe dans le dépôt

```
core/                 C++20
  domain/             logique métier pure — ne lie ni JUCE ni Tracktion ni ui
  engine/             adaptateur domain ↔ Tracktion Engine (vide, README seulement)
  ui/                 affichage — Tokens, RootView, tokens/tokens.json
  app/                point d'entrée JUCE + Tracktion, fenêtre principale
  tests/              test fumée du domaine, lancé par ctest
services/             paquet Python daw_services
  rpc, mcp, harmony, conditioning, ia_provider   (squelettes documentés)
workspaces/           schema/workspace.schema.json + decouverte, beatmaker, ugc, film
external/             JUCE, tracktion_engine, clap, vst3sdk (sous-modules)
cmake/                DawWarnings, DawGuards, DawExternal
scripts/              setup-ubuntu.sh, setup-windows.ps1, bootstrap-submodules.sh,
                      check-format.sh, check_hygiene.py, validate_workspaces.py
.github/workflows/    ci.yml
```

## 3. Versions épinglées

| Sous-module | Commit / tag | Remarque |
|---|---|---|
| JUCE | `37c894f` (8.0.13) | commit exact référencé par Tracktion dans `modules/juce` |
| tracktion_engine | `00fe427` (3.5.0, develop) | seul `modules/` est ajouté, pas sa copie interne de JUCE |
| clap | `1.2.10` | header-only |
| vst3sdk | `v3.8.1_build_84` (`3cdf9ca`) | `base`, `pluginterfaces`, `public.sdk`, `cmake` uniquement |

Règle de mise à jour : JUCE suit le commit que Tracktion référence, jamais une version choisie indépendamment.

## 4. Les trois règles d'hygiène, et leur application automatique

1. **Aucune valeur visuelle en dur.** `core/ui/tokens/tokens.json` est embarqué dans le binaire et lu par `daw::ui::Tokens`. Couleur de fond, taille par défaut et taille minimale de la fenêtre viennent des tokens. `scripts/check_hygiene.py` refuse les couleurs, tailles, rayons et tailles de police écrits en dur dans `core/ui` et `core/app`. Seul `core/ui/src/Tokens.cpp` est exempté, c'est lui qui interprète les tokens.
2. **Aucun panneau ne connaît sa position ou sa taille.** Seuls les hôtes de layout (`*View`, `*Layout*`, `core/ui/layout/`, fenêtres) ont le droit d'appeler `setBounds` et équivalents. Vérifié par le même script.
3. **Logique métier séparée de l'affichage.** `cmake/DawGuards.cmake` fait échouer le configure si `daw_domain` est lié à JUCE, Tracktion ou `ui`. Le script refuse aussi les includes de ces bibliothèques dans `core/domain`.

Les trois vérifications tournent dans le job `Checks` de la CI.

## 5. Vérifications réellement faites

- **Build Windows (local) :** `cmake --preset windows-msvc`, build, `ctest` — 1/1 test passé. `DAW IA.exe` s'ouvre, la fenêtre « DAW IA » s'affiche, Tracktion Engine démarre sans erreur.
- **Build Linux clang (CI) :** configure, build, ctest passés.
- **CI complète :** run #2 vert, 5 jobs — `Checks`, `Services` ×2, `Core` ×2. Cache sccache à 96 % de succès côté Linux.
- **Services Python :** ruff et pytest verts sur Ubuntu et Windows, 7 tests.
- **Manifestes :** validés contre le JSON Schema, plus les règles croisées (panneau déclaré = panneau placé, une seule fois).
- **Outillage Windows :** `setup-windows.ps1` exécuté en dry-run puis en réel sur la machine de dev.

## 6. Problèmes rencontrés et corrigés

| Problème | Cause | Correctif |
|---|---|---|
| CI rouge au run #1 | `astral-sh/setup-uv@v10` — ce tag majeur n'existe pas | épinglé sur `v10.1.0` (`79b8cd9`) |
| Avertissement MSVC `D9025` sur chaque fichier | JUCE exige CMake 3.22, donc `CMP0141` inactive dans son scope, et JUCE ajoute `/Zi` par-dessus notre `/Z7` | `set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)` (`ef46c5c`) |
| Installation des Build Tools incomplète | téléchargements échoués, dont `Win11SDK_10.0.26100` (code 1603) | reprise par `setup.exe modify`, cas géré dans `setup-windows.ps1` |

## 7. Points ouverts pour la S2

- **Script Ubuntu non validé en conditions réelles.** Le mode `--ci` saute trois parties : installation de CMake via le dépôt Kitware, installation de `uv`, et clang par défaut. À lancer une fois sur la machine Ubuntu.
- **Protection de `main` impossible.** Les rulesets ne s'appliquent pas aux dépôts privés sur le plan GitHub Free. Décision du 16 septembre : rester sans protection. Le travail se fait donc en push direct sur `main`.
- **`core/engine/` est vide.** Aucun code ne relie encore le domaine à Tracktion.
- **Le Command Bus n'existe pas encore.** `core/domain` ne contient qu'un `BuildInfo` et son test fumée. C'est le vrai premier chantier de code.
- **Pas de framework de test.** Le test du domaine est un `main()` qui renvoie un code de sortie. À remplacer par Catch2 ou doctest quand le Command Bus arrivera.
- **`-Werror` limité au domaine.** Les sources JUCE et Tracktion compilent dans la cible de l'app, donc les avertissements y restent visibles mais non bloquants.
- **Avertissement `C4702`** dans `tracktion_WaveInputDevice.cpp` : code de Tracktion, laissé tel quel.
- **Liste de livrables S1 possiblement incomplète.** La demande initiale s'arrêtait au milieu du point 6 (« clang-format, »). Si d'autres outils étaient prévus (clang-tidy, pre-commit, documentation), ils n'ont pas été traités.

## 8. État par rapport au plan 26 semaines

La S1 visait un socle vide mais complet : outillage, structure, CI. C'est atteint, avec une semaine qui se termine sur une CI verte sur les deux plateformes et une application qui démarre. Aucune décision d'architecture n'a été rouverte. Rien de fonctionnel n'existe encore côté produit : pas de Command Bus, pas de versioning, pas de services Python réels, pas d'IA.

**Suite logique en S2 :** le Command Bus dans `core/domain`, avec sérialisation, undo et redo, plus un vrai framework de test. C'est la brique dont dépendent le versioning, le pilotage MCP et le reste.
