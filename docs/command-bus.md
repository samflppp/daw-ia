# Le Command Bus

Contrat de la brique centrale de `core/domain`. Toute mutation de l'état du
projet passe par un objet commande sérialisable, sans exception : c'est ce qui
donne l'annulation, le versioning et le pilotage MCP avec un seul mécanisme.

## Les deux formes sérialisées

Une commande a **deux** formes, et la distinction est le cœur du modèle.

| Forme | Contenu | Produite par | Consommée par |
|---|---|---|---|
| `payload()` | l'intention, et rien d'autre | la commande | le rejeu, JSON-RPC, MCP |
| `undoRecord` | l'état que la commande a écrasé | `apply()` | `revert()` |

Une commande reconstruite depuis son seul `payload` est exécutable : c'est ce
qui rend le rejeu possible. Elle n'est pas **annulable** seule — l'annulation a
besoin de l'`undoRecord`, qui n'existe qu'une fois la commande exécutée. Le bus
garde les deux côte à côte ; les deux sont des `Value`, donc persistables.

**Conséquence dure :** une commande n'invente jamais d'identifiant dans
`apply()`. L'appelant engendre `clipId` et `noteId` avant l'exécution et les met
dans le payload. Sinon, rejouer le même payload donnerait un projet différent.

## L'enveloppe

```json
{
  "v": 1,
  "id": "01K5X8Q2R7M3N0P4V6W8Y9Z1AB",
  "type": "note.add",
  "at": 1758182400123456,
  "gesture": null,
  "payload": { "clipId": "...", "id": "...", "pitch": 60, "velocity": 100,
               "startBeats": 0.0, "lengthBeats": 0.25 }
}
```

- `id` : ULID, 26 caractères. Triable par date de création, clé SQLite directe.
- `at` : micro-secondes UTC depuis l'epoch, en entier. Pas de texte, pas de locale.
- `type` : le nom listé dans les manifestes de `workspaces/`.

`CommandRegistry` associe un `type` à une fabrique. Sans lui, une commande
sérialisée n'est que du texte ; avec lui, elle redevient exécutable, dans un
autre processus ou un autre jour.

## Le coalescing

Explicite, décidé par l'appelant, sans minuterie — le domaine ne connaît pas
l'horloge d'interaction.

```cpp
const auto geste = bus.beginGesture("fader volume piste 3");
for (chaque image)
    bus.execute(std::make_unique<SetTrackVolume>(piste, dB), {.gesture = geste});
bus.endGesture(geste);
```

Fusion si et seulement si les trois conditions tiennent :

1. l'appelant passe un `gesture` et c'est celui qui est ouvert ;
2. l'entrée en tête d'historique porte le même `gesture` ;
3. `canCoalesceWith()` du type en tête accepte la nouvelle commande.

Les deux moitiés doivent être d'accord : le type dit s'il *peut*, l'appelant dit
s'il *veut*. Rien ne fusionne par accident.

L'entrée fusionnée garde l'`id`, la date et l'`undoRecord` de la **première**
commande du geste, et le `payload` de la **dernière**. Soixante images de fader
donnent une entrée d'historique, une enveloppe au journal, et une annulation qui
revient avant le geste.

`endGesture` est obligatoire. Après fermeture, la commande suivante ouvre une
nouvelle entrée, même type et même cible compris. Ouvrir un second geste ferme
le premier ; une annulation ferme le geste ouvert.

## Invariants du bus

- Une commande réussie vide la pile de redo.
- Une commande en échec ne laisse aucune trace : état inchangé, pas d'entrée,
  pas de notification de succès. Les commandes valident avant de muter.
- Les observateurs sont notifiés après la mutation, avant le retour de la
  méthode. Appeler `execute`, `undo` ou `redo` depuis un observateur échoue avec
  `ErrorCode::reentrantCall`.
- Au-delà de `BusLimits::maxUndoDepth` (512 par défaut), l'entrée la plus
  ancienne est retirée et `onHistoryTruncated` est émis. Tronquer l'historique
  ne touche jamais le projet.
- Le domaine ne lève aucune exception. Tout retourne `Result<T>`.

## Le journal

`journal()` retourne les enveloppes qui construisent l'état courant, dans
l'ordre, après fusion. Les rejouer sur un projet vide reproduit l'état à
l'identique, identifiants et dates compris. C'est le format que la couche de
versioning persistera.

## Les trois commandes de la S2

| Type | Ce qu'elle valide |
|---|---|
| `clip.create_midi` | création structurelle, identifiant fourni par l'appelant, rejeu déterministe |
| `note.add` | mutation imbriquée, échec propre si le clip n'existe pas |
| `track.set_volume` | mutation continue, seule des trois à accepter le coalescing |

## Ce que le domaine ne lie pas

`core/domain` ne lie ni JUCE, ni Tracktion, ni `core/ui`. `cmake/DawGuards.cmake`
fait échouer le configure si c'est le cas, et `scripts/check_hygiene.py` refuse
les includes correspondants.

nlohmann/json est utilisé **uniquement** dans `src/serialization/Json.cpp`, lié
en `PRIVATE`, et n'apparaît dans aucun en-tête : le reste du projet ne l'hérite
pas, et il est remplaçable sans toucher une seule commande. Les commandes ne
manipulent que `daw::domain::Value`.
