# DAW IA — reprise de contexte

Ce fichier sert à ce qu'une session Claude Code neuve reprenne le projet sans rien redemander.
Contexte produit et stratégique complet : `docs/contexte-projet.md`.

## 1. Le projet en cinq lignes

DAW assisté par IA : socle natif Tracktion Engine + JUCE (`core/`), services Python séparés
(`services/`, JSON-RPC sur socket local), workspaces déclaratifs (`workspaces/`). Un copilote pilote
mixage, routage et paramètres via le Command Bus ; un moteur génératif (à venir) injecte du MIDI et de
l'audio par le même bus. Windows est la seule cible produit. Projet mené en solo, en incubateur, vers un
comité en mars 2027, sur un plan de 26 semaines dont chaque semaine ferme sur un bilan dans `docs/`.

**Cap actuel.** Jalon go/no-go atteint en S8. S18–S19 : ergonomie (zoom continu sur une seule toile
playlist/piano-roll, grammaire de navigation unique). Ensuite, d'autres fonctionnalités d'IA, puis le
fine-tune du modèle. Plus aucun verbe ajouté au domaine sans une raison de démonstration.

## 2. Décisions d'architecture acquises — ne jamais rouvrir

| Décision | Semaine | Pourquoi |
|---|---|---|
| Command Bus : toute mutation de `ProjectState` passe par une commande sérialisable | S1–S2 | annulation, versioning et pilotage MCP avec un seul mécanisme |
| Aucune commande n'engendre d'identifiant dans `apply()` | S2 | l'appelant engendre `clipId`/`noteId` avant l'exécution ; sinon rejouer le même payload donnerait un projet différent |
| `ProjectState` est la seule vérité, l'`Edit` Tracktion n'est qu'une projection, `UndoManager` de Tracktion jamais utilisé | S3 | deux sources de vérité est le piège classique ; tranché une fois pour toutes |
| Projection par réconciliation, liée par identité (`TrackId`/`ClipId` dans le `ValueTree`), jamais par position | S3 | lier par position casse dès qu'un élément est supprimé au milieu ; coût borné et payé volontairement |
| Règle de thread du bus : le bus retient le thread qui l'a construit et refuse les autres (`ErrorCode::wrongThread`, vérifié en release) | S3 | `core/domain` ne peut pas demander « suis-je sur le thread message », notion de framework ; `rebindToCurrentThread()` couvre la passation délibérée |
| Journal append-only distinct de `journal()` (vue de la pile d'undo) | S5 | `core/persistence` garde tout, y compris commandes annulées et annulations elles-mêmes — voir `docs/persistence.md` |
| Pattern / Placement / Clip / Lane séparés, liaison par identifiant jamais par rang | S9–S17 | `pattern.create`+`ownLane`, `pattern.place`, `Lane{LaneId,name}` dans `ProjectState` ; réordonner les lignes ne réécrit aucun bloc |
| Windows seule cible | S1 (annoncé), acté S5 | support Linux/Ubuntu reporté post-MVP ; `setup-ubuntu.sh` reste pour la CI, jamais lancé en cible produit |

Rouvrir une de ces lignes veut dire que quelque chose de nouveau la contredit réellement — c'est couvert
par la règle de méthode « dire quand un choix contredit l'acquis » (§4), pas une réévaluation de confort.

## 3. Invariants vérifiés automatiquement

Trois règles d'hygiène, vérifiées en CI :

1. **Aucune valeur visuelle en dur** (couleurs, espacements, tailles, rayons, police) — tout vient de
   `core/ui/tokens/tokens.json` via `daw::ui::Tokens`.
2. **Aucun panneau ne connaît sa position ou sa taille** — seuls les hôtes de layout (`*View`,
   `*Layout*`, `core/ui/layout/`, fenêtres) appellent `setBounds` ou lisent la géométrie du parent/écran.
3. **`daw_domain` ne lie et n'inclut ni JUCE, ni Tracktion, ni ui.**

Mécanismes :
- `scripts/check_hygiene.py` — analyse statique des trois règles sur `core/`, plus une règle 4 :
  aucun point-virgule dans un nom de `TEST_CASE` (un point-virgule coupe le nom en liste ctest et le
  filtre ne matche plus rien — quatre tests avaient cessé de tourner silencieusement pour cette raison).
- `cmake/DawGuards.cmake` — échoue la configuration si une cible lie `juce::`, `tracktion::`, `daw_ui`
  ou `daw_app`.
- Test croisé registry ↔ table de descriptions du copilote, **dans les deux sens** (S8–S9) : une
  commande ajoutée au `CommandRegistry` sans entrée dans la table du copilote casse le test, et
  inversement.

## 4. Règles de méthode — tenues depuis dix-huit semaines

- **Exposer avant de coder.** Une décision de modèle ou de comportement se présente et se discute avant
  d'être écrite, jamais découverte dans le diff.
- **Un test qui interroge l'état ne prouve pas l'effet.** Trouvé en S3 sur `EngineHostTests` : un test
  qui lit une propriété d'un composant peut être vert alors que le composant ne produit rien. Verrouiller
  la propriété à la bonne couche, pas à la couche qui la lit.
- **Un test neuf qui passe du premier coup est cassé une fois.** On casse volontairement le code qu'il
  regarde pour vérifier qu'il tombe au rouge ; sinon il est aveugle (S17 : un test qui comparait deux
  payloads perdant le nom de la même façon a été trouvé de cette manière).
- **Dire quand un choix contredit l'acquis.** Un écart au modèle établi est nommé explicitement dans le
  bilan de la semaine (voir « Les écarts à l'acquis, dits » dans chaque `docs/bilan-sN.md`), jamais
  glissé en silence.
- **`./scripts/check-all.sh` avant chaque commit.** Lance clang-format, hygiène, validation des
  manifestes workspaces, ruff, pytest — tout ce que la CI vérifie sauf le build et les tests C++
  (les presets, qui prennent des minutes).
- **Commits atomiques.** Un commit, un changement ; un diff qui touche plusieurs sujets à la fois est
  noté comme l'écart qu'il est.
- **`IDEES.md` se remplit et ne se met pas en œuvre.** C'est une décision de roadmap consignée, pas un
  chantier ; rien n'y est construit tant qu'une semaine ne le prend pas explicitement dans son périmètre.

## 5. Où trouver quoi

- **Bilans hebdomadaires** : `docs/bilan-sN.md` (S1 à S18 au 01/10/2026, plus `bilan-s18bis.md`), un par semaine, plus
  `docs/bilan-s7bis.md`. Chaque bilan documente les écarts à l'acquis, les tests cassés une fois, et le
  reste à faire.
- **Roadmap non engagée** : `IDEES.md` — couche décision du copilote, couche générative, recherche de
  samples par IA, stratégie du moteur génératif.
- **Modèle du Command Bus** : `docs/command-bus.md`.
- **Persistance et journal append-only** : `docs/persistence.md`.
- **Plugins VST3/CLAP** : `docs/plugins.md`.
- **Presets CMake** (`CMakePresets.json`) : `linux-clang` / `linux-clang-release` (CI), `windows-msvc` /
  `windows-msvc-release` (cible produit), `domain-only` (le domaine seul, sans JUCE ni sous-modules).
- **Variantes de `--verify`** (auto-vérification de l'application, rendu audio compris) : `--verify`
  (parcours complet), `--verify-reopen` (fermer/rouvrir dans un autre processus), `--verify-legacy`
  (compatibilité d'un ancien projet), `--verify-file` (menu Fichier, jusqu'à un vrai « Enregistrer
  sous »).
- **Clé d'API** : variable d'environnement `DAW_IA_ANTHROPIC_API_KEY`, jamais en dur ni dans un fichier
  du dépôt (vérifié en S17 : aucune clé, aucun `.env`, dans tout l'historique).
- **Tests d'un plugin réel** : label ctest `audio`, exclus de la CI ; `DAW_TEST_VST3` / `DAW_TEST_CLAP`
  en variables d'environnement.

## 6. Dettes ouvertes

| Dette | Depuis | État |
|---|---|---|
| Bug intermittent de la lecture, instrumenté | S12 | ne s'est pas présenté depuis S13, pas fermé |
| Écoute de la zone multi-pistes (accords/basse/mélodie ensemble, à l'oreille) | S17 | pas encore faite, décrite comme le test le plus important de la semaine |
| « Ranger le projet » comme geste explicite | S17 (§6) | demandé en cours de semaine, pas engagé |
| Fine-tune sur un corpus personnel du fondateur | tranché S15 | écarté définitivement — un générateur enfermé dans le style d'un seul producteur ne sert que lui |
| Stratégie complète du moteur génératif au-delà de S15 | ouvert depuis S14 | partiellement tranchée (S15) ; le reste (empiler les couches, Markov vs neuronal) reste à trancher |
| L'application qui ne quitte pas à la fermeture (fenêtre partie, processus vivant) | S18 | vu une fois, non diagnostiqué |
| La toile ne remplace pas encore le piano-roll (vélocités, copier-coller, génération dans une bande, mode pattern) | S18 | noté dans `IDEES.md` |
| Support Ubuntu en cible produit | reporté S1/S5 | scripts gardés pour la CI seulement |
