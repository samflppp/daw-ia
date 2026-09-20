# La persistance

Le projet survit à la fermeture de l'application. Ce document dit comment, et
pourquoi de cette façon plutôt qu'une autre.

## 1. Un projet est un dossier

```
Mon projet.dawproj/
    project.db        le journal, SQLite en mode WAL
    blobs/aa/aabb…    le magasin adressé par contenu (BLAKE3-256)
```

Copier le dossier copie le projet, y compris l'état opaque des plugins. C'est
la raison pour laquelle le magasin de blobs a quitté `AppData` en S5 : il est
état de projet, pas état machine.

`project.db-wal` et `project.db-shm` apparaissent à côté de la base tant
qu'elle est ouverte. `ProjectStore::close()` fait un checkpoint `TRUNCATE` et
les efface, donc un projet fermé est un fichier plus un dossier de blobs.

`core/persistence` ne lie ni JUCE ni Tracktion : ses tests tournent dans le
preset `domain-only`, sans périphérique audio.

## 2. Le journal est append-only

```sql
CREATE TABLE journal (
    seq              INTEGER PRIMARY KEY AUTOINCREMENT,
    kind             TEXT    NOT NULL CHECK (kind IN ('execute','coalesce','undo','redo')),
    command_id       TEXT    NOT NULL,
    at_micros        INTEGER NOT NULL,
    actor            TEXT    NOT NULL CHECK (actor IN ('user','copilot','generator')),
    context_digest   TEXT,
    context_bytes    INTEGER,
    type             TEXT,
    gesture_id       TEXT,
    payload          TEXT,
    envelope_version INTEGER NOT NULL
);
```

Rien n'est jamais modifié, rien n'est jamais supprimé.

- **Une commande annulée garde sa ligne**, et l'annulation garde la sienne. La
  pile d'undo est une vue dérivée de la suite, pas une table. C'est ce qui rend
  une branche d'arrangement possible : une branche est un pointeur dans
  l'historique complet, et un historique effacé n'a nulle part où pointer.
- **Une fusion de geste s'écrit en ligne nouvelle** (`kind = 'coalesce'`), pas
  en `UPDATE`. Un balayage de fader laisse donc toutes ses valeurs
  intermédiaires. Le chargeur ne garde que la dernière, qui est la forme
  fusionnée que le bus rejouerait.
- **Les annulations portent leur propre acteur.** Un undo demandé par le
  copilote est un fait ; il ne réécrit pas l'auteur de l'entrée annulée.
- **Les commandes transitoires ne sont pas écrites.** Rejouer un
  `transport.play` ferait qu'un projet se met à jouer en s'ouvrant.

Il n'y a **pas de table de blobs**, et il n'y en aura pas : le magasin BLAKE3
est déjà la table adressée par contenu. `context_digest` est du texte sans clé
étrangère, exactement comme un digest dans un payload.

`meta` porte `schema_version`, `project_id` (un ULID), `created_at_micros`.

## 3. Le repli des fusions

Le chargeur replie chaque ligne `coalesce` sur l'entrée qui la précède. C'est
sûr parce que le bus ne fusionne qu'avec le sommet de la pile d'undo : toute
ligne `execute`, `undo` ou `redo` intercalée casse la fusion. Un journal où une
ligne `coalesce` ne suit pas son exécution a été écrit par autre chose, et le
chargeur le dit (`storageError`) au lieu de rejouer un historique faux.

## 4. Un seul écrivain

`ProjectStore` observe le bus, donc il écrit depuis le thread du bus et de là
seulement — un seul écrivain par construction, pas par discipline. WAL laisse
un lecteur lire pendant ce temps : c'est ce dont S6 aura besoin pour afficher
une branche, et ce dont la couche d'extraction pour modèle aura besoin plus
tard.

Un observateur ne peut pas retourner de `Result`. Une écriture qui échoue est
donc retenue, et la première est celle qui est rapportée — les suivantes n'en
sont que les conséquences. `close()` la rend, pour que « ce projet n'a pas été
entièrement enregistré » soit une phrase et non un silence.

## 5. Migration

`meta.schema_version` vaut 1 dès la création, et la chaîne de migration existe
dès la v1 : un chemin de migration ajouté le jour où la première colonne change
est un chemin qui n'a jamais tourné, et il casse précisément les projets qu'il
devait sauver.

| Version lue | Ce qui se passe |
|---|---|
| aucune / base vide | création, `schema_version = 1` |
| < version courante | migrations appliquées une par une, une transaction par pas |
| = version courante | rien |
| > version courante | refus explicite, pas d'ouverture dégradée |

Une base tronquée, un fichier qui n'est pas une base, un payload illisible :
`Result` en erreur nommant la ligne fautive, jamais de crash, jamais de
réparation automatique.

## 6. Ce que prouvent les tests

Un chargeur appelé deux fois dans le même processus ne prouve rien : le second
appel peut lire ce que le premier a laissé en mémoire, en cache ou dans une
connexion ouverte. Les binaires de test se relancent donc **en processus
enfant** : l'enfant écrit le projet et meurt, le parent rouvre et mesure.

| Cas | Ce qui est mesuré |
|---|---|
| Aller-retour complet | l'état JSON écrit par l'enfant est celui que le parent reconstruit |
| Undo après rechargement | la pile d'undo est là, et le geste de fader y est une entrée, pas vingt |
| Commande annulée | elle reste annulée, et reste à un redo près |
| Provenances mêlées | `user`, `copilot`, `generator` rejouées et interrogeables en SQL |
| Projet volumineux | 5 002 commandes relues en 2,4 s |
| Base tronquée, fichier corrompu, schéma du futur | erreur nommée, aucun crash |
| Enveloppe v1 | rejouée comme `user` |
| Dossier copié | s'ouvre ailleurs et rend le même état |
| **Plugin** (`core/engine`, label `audio`) | le RMS rendu après rechargement est celui d'avant : blob puis paramètres épars, dans cet ordre |
