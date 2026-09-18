# Bilan de fin de S3 — DAW IA

**Période :** semaine 3 sur 26 (29 septembre – 5 octobre 2026). Rédigé le 18 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 4 commits (`d1e0a56` → `6724f1f`).
**CI :** run #7 vert, 5 jobs, 6 min 29 s.
**Volume :** 32 fichiers, +1 557 lignes. `core/engine` passe d'un README à 11 fichiers.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Cible `daw_engine` | Livré | `core/engine/CMakeLists.txt`, bibliothèque INTERFACE |
| 2 | `EngineHost` : possède l'`Engine` et l'`Edit` | Livré, vérifié | `EngineHostTests.cpp` |
| 3 | `ProjectProjector` : projection par réconciliation | Livré, vérifié | `ProjectionTests.cpp`, 9 cas |
| 4 | `TransportController` : play, stop, set_position | Livré, vérifié | `TransportProjectionTests.cpp`, 5 cas |
| 5 | Les vraies bornes, reprises de Tracktion | Livré | volume `[-100, +6]` dB au lieu de `[-60, +12]` |
| 6 | Preuve audible | **Livré, confirmé à l'oreille** | `DAW IA.exe --demo` — do–mi–sol entendu |

Ajouts non prévus lundi : `EngineHostTests.cpp` (né du débogage du silence) et `ThreadingTests.cpp` (dette annoncée en S3, payée dans la semaine).

## 2. Les trois décisions arbitrées en début de semaine

| Question | Décision | Ce que ça a donné |
|---|---|---|
| Comment projeter | **Réconciliation** | Undo, redo, coalescing et troncature marchent sans une ligne de code d'engine. Le test qui le prouve annule puis rejoue alors qu'aucun fichier de `core/engine` ne contient le mot « annuler ». |
| Le transport | **`HistoryPolicy::transient`** | Le transport passe par le bus, est validé et notifié, mais ne crée ni entrée d'historique ni entrée de journal, et ne vide pas la pile de redo. |
| Les tests d'engine | **Exclus de la CI par label** | Compilés partout, exécutés là où il y a un périphérique audio. Coût CI réel : **zéro** (voir §6). |

## 3. La décision qui n'était pas au programme

Tracktion a déjà un état — l'`Edit`, un `juce::ValueTree` — et déjà un `UndoManager`. Le domaine a le sien. Deux sources de vérité, c'est le piège classique. Il est tranché : **`ProjectState` est la seule vérité, l'`Edit` est une projection**, et l'`UndoManager` de Tracktion n'est jamais utilisé — `nullptr` partout où il est attendu.

En écrivant le projector, une deuxième décision s'est imposée, que je n'avais pas exposée lundi : **la liaison se fait par identité, jamais par position**. Lier la piste *n* du domaine à la piste *n* de l'`Edit` casse dès qu'on supprime une piste au milieu. Chaque piste Tracktion porte donc le `TrackId` du domaine dans son `ValueTree`. Test dédié.

Le coût de la réconciliation est payé volontairement, et il est borné :
- une piste dont la forme sérialisée n'a pas changé est sautée entièrement ;
- ses clips ne sont reconstruits que si les clips ont changé.

Trente images de fader ne touchent pas un seul clip — vérifié par identité de pointeur, pas par comptage.

## 4. Le bogue de la semaine : ça ne faisait aucun son

**Symptôme.** `--demo` était muet, alors que les 14 tests d'engine passaient. Ils prouvaient que le transport tourne et que les notes sont dans le clip. Ils ne prouvaient pas qu'un échantillon sort de la machine — et c'est exactement l'écart qui s'entendait.

**Mesure**, au retour du constructeur d'`EngineHost` :

```
wave out devices: 0
default wave out: none
current audio device: Haut-parleur (Realtek(R) Audio)
device open: true, device playing: true
```

Le périphérique JUCE est ouvert et tourne ; la liste des wave devices de Tracktion est vide.

**Cause racine.** `DeviceManager::deviceListChanged()` appelle `triggerAsyncUpdate()` : la liste est construite dans `handleAsyncUpdate()`, donc seulement après un tour de boucle de messages. Un `runDispatchLoopUntil(300)` fait passer le compte de **0 à 4** — cause confirmée par mesure, pas déduite. L'application appelait `transport.play()` depuis `initialise()`, avant que la boucle ne tourne : le contexte de lecture était bâti sans aucune sortie.

**Correctif à la racine.** `EngineHost` appelle `dispatchPendingUpdates()` avant de créer l'`Edit`. Le constructeur veut alors dire ce qu'il annonce : quand il retourne, le moteur est utilisable. Attendre la boucle de messages aurait fait dépendre « le moteur est-il prêt » de qui appelle et quand.

**Leçon retenue, et elle vaut au-delà de ce bogue :** un test qui interroge l'état d'un composant ne prouve pas que le composant produit son effet. Les deux cas d'`EngineHostTests` verrouillent maintenant la propriété à la bonne couche, et ils échouaient tous les deux avant le correctif (`0 > 0`, `nullptr != nullptr`).

## 5. La règle de thread, vérifiée au lieu d'être documentée

Annoncée lundi, écrite dans la foulée. Le projector mute un `Edit` depuis les notifications, et Tracktion attend ça sur le thread message.

Le domaine ne peut pas demander « suis-je sur le thread message » : c'est une notion de framework, et `core/domain` ne lie pas JUCE. Il retient donc **le thread qui l'a construit** et refuse les autres. Même règle, exprimée sans framework.

`ErrorCode::wrongThread`, vérifié **en release aussi** — une comparaison d'identifiant de thread, et le bus retourne déjà `Result` partout. Le contrôle passe avant tout le reste : une enveloppe malformée envoyée du mauvais thread signale le thread, pas l'enveloppe.

| Méthode | Garde |
|---|---|
| `execute`, `executeSerialized`, `undo`, `redo`, `endGesture` | `Result` → `wrongThread` |
| `beginGesture`, `clearHistory`, `addObserver`, `removeObserver` | `assert` en debug uniquement |

Les quatre dernières ne retournent pas de `Result` et gardent leur signature. Aucune ne mute le projet, et la commande qui le ferait doit passer par `execute()`. La sécurité est entière ; l'uniformité ne l'est pas.

`rebindToCurrentThread()` couvre la passation délibérée — charger un projet sur un thread de travail, puis confier le bus au thread message. Refusée depuis un autre thread que le propriétaire actuel : une passation est une décision, jamais une course.

## 6. Vérifications réellement faites

| Vérification | Résultat |
|---|---|
| `domain-only` (MSVC local) | build, **61/61** |
| `windows-msvc` (JUCE + Tracktion, local) | build, **61/61** hors label `audio` |
| `ctest -L audio` en local | **16/16**, contre un vrai `Engine` et un vrai `Edit` |
| CI run #7 (Linux clang + Windows MSVC) | 5 jobs verts, 6 min 29 s |
| `check-format.sh`, `check_hygiene.py` | verts |
| Avertissements | zéro sur le code du projet |
| **Écoute** | do–mi–sol entendu, confirmé par l'utilisateur |

**Coût CI de la seconde compilation de Tracktion : nul.** Run #5 (S2) et run #7 (S3) durent tous deux 6 min 29 s. sccache partage les objets entre `daw_app` et `daw_engine_tests`. L'option `DAW_BUILD_ENGINE_TESTS` envisagée lundi est donc inutile : on garde les tests compilés en CI, c'est ce qui attrape une rupture d'API Tracktion même sans périphérique audio.

77 `TEST_CASE` au total, 432 assertions.

## 7. Les vraies bornes

Lues dans le code de Tracktion, plus devinées :

| Borne | S2 (mon goût) | S3 (Tracktion) | Source |
|---|---|---|---|
| Volume | [-60, +12] dB | **[-100, +6] dB** | `volumeFaderPositionToDB()` : position 1.0 → +6 dB, 0 → -100 dB |
| Tempo | [20, 300] BPM | [20, 300] BPM | `TempoSetting::minBPM` / `maxBPM` — juste par chance |

## 8. Points ouverts pour la S4

- **`journal()` n'est pas un log d'audit** (hérité de la S2). Après une annulation, la commande annulée sort du journal. Cohérent avec « rejouer reproduit l'état courant », pas avec « le journal garde tout ce qui a été fait ». **Cette question doit être tranchée en S4 si la S4 est le versioning**, parce que c'est elle qui décide de ce que SQLite stocke.
- **L'`Edit` n'est jamais écrit sur disque, et c'est voulu.** La sauvegarde persistera le journal de commandes, pas un fichier d'`Edit` — l'`Edit` est une projection, pas la vérité.
- **Réordonner les pistes ne se projette pas.** L'ordre des pistes dans l'`Edit` suit l'ordre de création, pas celui du domaine. Sans conséquence audio, à reprendre quand l'UI affichera un ordre.
- **Pas d'hébergement VST3 ni CLAP.** Un `FourOscPlugin` par piste, c'est tout ce qui produit du son aujourd'hui.
- **Le configure a besoin du réseau au premier lancement** (deux archives FetchContent, mises en cache ensuite).
- **Script Ubuntu toujours non validé en conditions réelles** (hérité de la S1).
- **Aucun enregistrement, aucun import de fichier audio.** Le projet ne sait que jouer ce que les commandes ont créé.

## 9. État par rapport au plan 26 semaines

La S3 visait le plus gros inconnu technique du projet : faire piloter un vrai moteur audio par le Command Bus. C'est atteint, et la preuve est audible, pas seulement verte.

Le choix de la réconciliation est validé par l'usage : annuler, refaire, fusionner un geste et tronquer l'historique fonctionnent tous sans une ligne de code dédiée dans `core/engine`. C'est le bénéfice qui avait justifié le choix lundi, et il s'est matérialisé.

Ce qui n'existe toujours pas, et c'était hors périmètre : aucune persistance, aucune interface, aucun service Python réel, aucune IA. Fermer l'application perd tout.

**Trois suites possibles en S4**, à arbitrer : le versioning SQLite (le journal survit à la fermeture), les services Python JSON-RPC (le bus devient pilotable de l'extérieur, socle du MCP), ou l'UI (les quatre workspaces déclarés en S1 deviennent visibles).
