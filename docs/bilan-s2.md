# Bilan de fin de S2 — DAW IA

**Période :** semaine 2 sur 26 (22–28 septembre 2026). Rédigé le 18 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 2 commits (`b02e87a`, `6e17f37`).
**CI :** run #5 vert, 5 jobs, 6 min 29 s.
**Volume :** 45 fichiers, +4 219 lignes. `core/domain` passe de 2 fichiers à 28.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Vrai framework de test, intégré à ctest et à la CI | Livré, vérifié | doctest 2.4.12, 48 entrées ctest, job `Core` vert sur les deux plateformes |
| 2 | Modèle de commande : interface, identité, horodatage, sérialisation aller-retour | Livré, vérifié | `command/Command.h`, `CommandEnvelope`, `SerializationRoundTripTests.cpp` |
| 3 | Bus : exécution, pile undo/redo, notification des observateurs | Livré, vérifié | `command/CommandBus.h`, `CommandBusTests.cpp`, `UndoRedoTests.cpp` |
| 4 | Trois commandes réelles | Livré | `clip.create_midi`, `note.add`, `track.set_volume` |
| 5 | Coalescing explicite, décidé par l'appelant | Livré, vérifié | `beginGesture`/`endGesture`, `CoalescingTests.cpp` |
| 6 | Tests : aller-retour, undo/redo, coalescing, invariants | Livré | 48 `TEST_CASE`, 20 `SUBCASE`, 290 assertions |

Ajout non demandé : `ReplayTests.cpp`, qui vérifie la propriété dont tout le reste dépend — un projet reconstruit depuis le seul journal sérialisé.

## 2. Les trois décisions arbitrées en début de semaine

| Question | Décision | Raison retenue |
|---|---|---|
| Framework de test | **doctest** | Header unique, rien à compiler ni à lier, donc le preset `domain-only` reste vrai. Catch2 v3 impose une lib compilée pour des matchers dont le domaine n'a pas l'usage. |
| Forme de l'annulation | **`undoRecord` séparé** | La commande inverse aurait forcé des types publics existant seulement pour annuler (`clip.restore` à identifiant imposé), qui auraient pollué la surface MCP. |
| Sérialisation JSON | **nlohmann en `PRIVATE`** | Battle-tested, et invisible : lié au seul `src/serialization/Json.cpp`, absent de tous les en-têtes, remplaçable sans toucher une commande. |

## 3. Le modèle, en une page

Une commande a **deux** formes sérialisées, et la distinction est le cœur du modèle.

| Forme | Contenu | Produite par | Consommée par |
|---|---|---|---|
| `payload()` | l'intention, et rien d'autre | la commande | rejeu, JSON-RPC, MCP |
| `undoRecord` | l'état que la commande a écrasé | `apply()` | `revert()` |

Une commande reconstruite depuis son seul `payload` est **exécutable** : c'est ce qui rend le rejeu possible. Elle n'est pas **annulable** seule — l'annulation a besoin de l'`undoRecord`, qui n'existe qu'une fois la commande exécutée. Les deux sont des `Value`, donc persistables côte à côte.

**Conséquence dure, testée :** une commande n'engendre jamais d'identifiant dans `apply()`. L'appelant met `clipId` et `noteId` dans le payload avant l'exécution. Sinon, rejouer le même payload donnerait un projet différent.

Enveloppe sérialisée :

```json
{
  "v": 1,
  "id": "01K5X8Q2R7M3N0P4V6W8Y9Z1AB",
  "type": "note.add",
  "at": 1758182400123456,
  "gesture": null,
  "payload": { "clipId": "…", "id": "…", "pitch": 60, "velocity": 100,
               "startBeats": 0.0, "lengthBeats": 0.25 }
}
```

`id` en ULID : 26 caractères, triable par date de création, clé SQLite directe. Pas `juce::Uuid`, interdit dans le domaine ; pas un compteur local au processus, qui ne veut plus rien dire une fois la commande rejouée. `at` en micro-secondes UTC, en entier : pas de texte, pas de locale.

`CommandRegistry` associe un `type` à une fabrique. Sans lui, une commande sérialisée n'est que du texte ; avec lui, elle redevient exécutable, dans un autre processus ou un autre jour.

## 4. Le coalescing

Explicite, décidé par l'appelant, **sans minuterie** : le domaine ne connaît pas l'horloge d'interaction.

```cpp
const auto geste = bus.beginGesture("fader volume piste 3");
for (chaque image)
    bus.execute(std::make_unique<SetTrackVolume>(piste, dB), {.gesture = geste});
bus.endGesture(geste);
```

La fusion demande l'accord des **deux moitiés** : le type dit s'il *peut* (`canCoalesceWith`), l'appelant dit s'il *veut* (`ExecuteOptions::gesture`). Rien ne fusionne par accident.

L'entrée fusionnée garde l'`id`, la date et l'`undoRecord` de la **première** commande du geste, et le `payload` de la **dernière**. Soixante images de fader donnent une entrée d'historique, une enveloppe au journal, une annulation.

Cinq cas de non-fusion testés : pas de geste, geste fermé, autre piste, commande structurelle, commande intercalée. Ouvrir un second geste ferme le premier ; une annulation ferme le geste ouvert.

## 5. Invariants du bus, tous testés

- Une commande réussie vide la pile de redo.
- Une commande en échec ne laisse **aucune** trace : état inchangé, pas d'entrée, pas de notification de succès. Les commandes valident avant de muter.
- Les observateurs sont notifiés après la mutation, avant le retour de la méthode.
- Un observateur qui rappelle `execute`, `undo` ou `redo` obtient `ErrorCode::reentrantCall`.
- Au-delà de `BusLimits::maxUndoDepth` (512 par défaut), l'entrée la plus ancienne part avec un `onHistoryTruncated`. Tronquer l'historique ne touche jamais le projet.
- Le domaine ne lève aucune exception. Tout retourne `Result<T>`.

## 6. Vérifications réellement faites

| Vérification | Résultat |
|---|---|
| `domain-only` (MSVC local, sans sous-modules) | configure, build, **48/48 ctest** |
| `windows-msvc` (JUCE + Tracktion, local) | configure, build, **48/48 ctest** |
| Commit `b02e87a` seul, en worktree | build + 1/1 ctest — aucun commit cassé au milieu de la série |
| CI run #5 (Linux clang + Windows MSVC) | 5 jobs verts, 6 min 29 s |
| `check-format.sh`, `check_hygiene.py` | verts |
| `validate_workspaces.py` | 4 manifestes valides |
| Avertissements | zéro, avec `/W4 /WX` et `-Wall -Wextra -Wpedantic -Wconversion -Wold-style-cast -Werror` |

Le rejeu est vérifié de bout en bout : une session de 34 commandes, journal de 5 enveloppes écrit en texte, relu, rejoué sur un projet neuf → même état, mêmes identifiants, mêmes dates, et l'historique rejoué s'annule encore.

## 7. Problèmes rencontrés et corrigés

| Problème | Cause | Correctif |
|---|---|---|
| `error C2280` sur l'affectation de `Value` | `std::variant` d'`unique_ptr` n'est pas copiable, et un `std::variant` de conteneurs d'un type incomplet n'est pas standard | tableaux et objets rangés derrière un `unique_ptr`, copie profonde écrite à la main |
| 40 erreurs dans `__msvc_string_view.hpp` | doctest affiche une comparaison ratée par `operator<<`, que MSVC ne définit pour `std::string_view` que si `<ostream>` est déjà là | `<ostream>` avant doctest dans `TestSupport.h`, avec le commentaire qui dit pourquoi |

## 8. Écarts par rapport au modèle validé lundi

Sept, tous mineurs, tous assumés :

1. `Value` range tableaux et objets derrière un `unique_ptr` (contrainte de compilation, cf. §7).
2. Le `payload` de `note.add` réutilise la forme de `Note` du snapshot plus `clipId` : un seul dialecte pour la note, donc l'identifiant y est `id` et non `noteId`.
3. `Receipt` porte aussi `gesture` et `redoDepth` — un observateur de barre d'état veut les deux.
4. `beginGesture(label)` prend une étiquette, lisible par `openGestureLabel()`, pour le futur libellé « Annuler le déplacement du fader ».
5. Une annulation ferme le geste ouvert ; ouvrir un second geste ferme le premier. Sinon une commande pourrait fusionner dans une entrée que l'undo vient de déplacer.
6. `executeSerialized` conserve l'`id` et la date de l'enveloppe et ne fusionne jamais : une enveloppe est déjà la forme fusionnée de son geste.
7. `journal()` renvoie la pile d'undo courante, pas un log d'audit.

## 9. Points ouverts pour la S3

- **`journal()` n'est pas un log d'audit.** Après une annulation, la commande annulée sort du journal. Cohérent avec « rejouer reproduit l'état courant », pas avec « le journal garde tout ce qui a été fait ». À trancher au moment de SQLite, pas avant.
- **Le configure a besoin du réseau au premier lancement** (deux archives FetchContent, mises en cache ensuite). Un build hors-ligne sur une machine neuve échoue. Vendoriser dans `external/` reste possible.
- **Bornes choisies faute de règle établie :** volume dans [-60, +12] dB, tempo dans [20, 300] BPM. À reprendre quand `core/engine` apportera les contraintes de Tracktion.
- **`core/engine` est toujours vide.** Le bus pilote un modèle en mémoire, aucun son n'est encore produit.
- **Script Ubuntu toujours non validé en conditions réelles** (point hérité de la S1 : le mode `--ci` saute CMake par Kitware, `uv` et clang par défaut).
- **Pas encore de commande de transport.** `transport.play` et `transport.stop` sont dans les manifestes mais n'existent pas : ce sont les premières qui n'auront pas de sens sans `core/engine`.

## 10. État par rapport au plan 26 semaines

La S2 visait la brique dont dépendent le versioning, le pilotage MCP et l'annulation. C'est atteint, et la propriété qui compte — un projet reconstructible depuis son seul journal sérialisé — est vérifiée par un test, pas seulement par le design.

Ce qui n'existe toujours pas, et c'était hors périmètre : aucun son, aucune interface, aucun service Python réel, aucune IA, aucune persistance sur disque. Le domaine mute un modèle en mémoire, rien de plus.

**Trois suites possibles en S3**, à arbitrer : `core/engine` (le bus pilote enfin Tracktion et le projet fait du bruit), le versioning SQLite (le journal survit à la fermeture de l'application), ou les services Python JSON-RPC (le bus devient pilotable de l'extérieur, socle du MCP).
