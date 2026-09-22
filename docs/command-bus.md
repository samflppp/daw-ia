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
  "v": 2,
  "id": "01K5X8Q2R7M3N0P4V6W8Y9Z1AB",
  "type": "note.add",
  "at": 1758182400123456,
  "gesture": null,
  "origin": { "actor": "copilot",
              "context": { "digest": "3f2a…", "byteCount": 8412 } },
  "payload": { "clipId": "...", "id": "...", "pitch": 60, "velocity": 100,
               "startBeats": 0.0, "lengthBeats": 0.25 }
}
```

- `id` : ULID, 26 caractères. Triable par date de création, clé SQLite directe.
- `at` : micro-secondes UTC depuis l'epoch, en entier. Pas de texte, pas de locale.
- `type` : le nom listé dans les manifestes de `workspaces/`.
- `origin` : qui a demandé la commande. `actor` vaut `user`, `copilot` ou
  `generator` ; `context` nomme par digest, dans le magasin adressé par
  contenu, ce sur quoi l'agent a agi — jamais par valeur.

### Versions

`v: 1` est lue et vaut `user` : c'est ce que ces commandes étaient. `v: 2` est
écrite, et une enveloppe v2 sans `origin` est refusée — elle est malformée, pas
ancienne.

La provenance est dans l'enveloppe et pas seulement dans la table SQLite, parce
qu'un rejeu lit des enveloppes : rangée en colonne, elle serait perdue dès qu'un
historique voyage en texte, par JSON-RPC ou dans un rapport de bogue.

Les deux couches d'IA prévues — un copilote qui pilote mixage, routage et
paramètres, un moteur génératif qui injecte du MIDI et de l'audio — passent par
le bus comme n'importe quel utilisateur. La provenance est la seule trace
qu'elles laissent, et le seul privilège qu'elles n'ont pas.

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
l'identique, identifiants et dates compris.

Ce n'est pas le journal sur disque, et la distinction est la décision de S5 :
`journal()` est une vue de la pile d'undo, `core/persistence` écrit une suite
append-only qui garde tout, y compris les commandes annulées et les annulations
elles-mêmes. Voir `docs/persistence.md`.

## Les commandes de structure

| Type | Ce qu'elle valide | Fusion |
|---|---|---|
| `track.add` | création d'une piste ; sans elle, une piste ne viendrait d'aucune commande et un rechargement la perdrait | non |
| `track.remove` | suppression ; l'undoRecord porte la piste entière et son index | non |
| `track.rename` | le nom est ce sur quoi un copilote reçoit un ordre ; l'undoRecord porte l'ancien | non |
| `track.reorder` | un index, jamais « monter » : un verbe relatif se rejoue autrement selon l'état | non |
| `track.set_volume` | mutation continue | par piste |
| `track.set_pan` | position dans le champ stéréo, jamais deux gains | par piste |
| `track.set_muted` | interrupteur, distinct du bypass de plugin | non |
| `track.set_channel_pitch` | la hauteur qu'un canal joue quand un pas s'allume ; ne touche aucune note déjà écrite | par piste |
| `pattern.create` | un pattern vide : du contenu, sans position | non |
| `pattern.place` | pose un pattern sur la timeline ; aucune note n'est copiée | non |
| `pattern.add_track` | ouvre la ligne d'une piste dans un pattern | non |
| `pattern.set_length` | longueur du pattern ; les notes au-delà sont gardées, jamais coupées | par pattern |
| `clip.create_midi` | S9 : un pattern d'une ligne **et** son placement, en une commande. Le payload n'a pas bougé, son sens oui — voir §modèle de pattern | non |
| `note.add` | mutation imbriquée, échec propre si la ligne n'existe pas | non |
| `note.remove` | l'undoRecord porte la note entière et son index | non |
| `note.move` | hauteur et départ, jamais la longueur | par note |
| `note.resize` | longueur, jamais le départ | par note |
| `note.set_velocity` | la vélocité se lisait, elle se change | par note |
| `note.quantize` | départs sur une grille en beats, longueurs intactes | non |
| `note.transpose` | un intervalle, refusé entier s'il sort de `[0, 127]` | non |

### Les verbes qui portent une liste

`note.quantize` et `note.transpose` prennent une liste d'identifiants de notes,
jamais « la sélection ». Une sélection vit dans l'interface : elle n'est ni
journalisée ni annulable, et deux fenêtres sur le même projet en auraient deux.
Une commande dont le sens en dépendrait ne se rejouerait pas, ne passerait pas
en JSON-RPC, et ne serait pas émissible par un copilote qui n'a pas de
sélection. L'interface lit la sienne et met les identifiants dans le payload ;
tout autre appelant fait exactement pareil.

Les deux sont **tout ou rien**. Chaque note est lue et vérifiée avant qu'une
seule bouge : une quantification qui en déplacerait la moitié avant de refuser
l'autre laisserait un undoRecord décrivant un état qui n'a jamais existé.

`note.transpose` refuse plutôt que de ramener dans les bornes : en ramenant,
descendre de douze demi-tons puis remonter rendrait un autre accord, et l'undo
cesserait d'être l'inverse de la commande. Son undoRecord porte l'intervalle et
non les hauteurs absolues, pour la même raison.

### Le panoramique et sa loi

`track.set_pan` porte une position dans `[-1, +1]` et jamais un couple de gains.
Transformer `-0,5` en un gain gauche et un gain droit est une décision de la
projection : un payload qui porterait des gains figerait la loi dans tous les
journaux déjà écrits.

La loi est `PanLaw3dBCenter`, l'une des cinq de Tracktion, écrite explicitement
sur chaque plugin de volume à chaque projection. Deux raisons, et aucune n'est
esthétique :

- `getDefaultPanLaw()` est une **globale mutable du processus**. Un projet dont
  l'image stéréo en dépendrait ne rendrait pas pareil sur deux machines, pour
  la même raison qu'une chaîne liée par index ne rechargerait pas pareil.
- ce défaut d'usine est `PanLawLinear`, qui calcule `R = g + pan·g` : à fond à
  droite, `R` vaut `2g`, soit une piste rendue **6 dB plus forte parce qu'elle
  est panoramiquée**. À puissance constante, le centre est 3 dB sous les
  extrêmes et traverser le champ ne change pas le niveau.

Mesuré sur rendu hors ligne, RMS par canal :

| Position | Gauche | Droite |
|---|---|---|
| `pan 0` | 0,110272 | 0,110272 |
| `pan -1` | 0,155863 | 0 |
| `pan +1` | 0 | 0,155490 |
| `pan +0,5` | rapport L/R mesuré **0,414214** | la loi dit **0,414214** |

Le rapport attendu est recalculé dans le test depuis la définition de la loi et
jamais demandé à Tracktion : un test qui demande à Tracktion ce que fait
Tracktion serait d'accord avec n'importe quelle loi.

**Largeur stéréo :** Tracktion n'offre pas de plugin de largeur — `width`
n'existe que comme paramètre interne du Chorus et du Reverb. Écartée plutôt
qu'écrite à la main.

**Mono :** le modèle n'a pas de clip audio, donc toute source est un instrument
MIDI et sort en stéréo. `VolumeAndPanPlugin` n'applique son gain droit que si
le tampon a deux canaux : un tampon mono serait **atténué et non déplacé**. Le
test affirme le nombre de canaux rendus, pour que ce soit un test rouge le jour
des clips audio et pas une surprise.

## Le tempo est une séquence

Depuis la S7 le projet ne porte plus un tempo scalaire mais une suite de
`TempoPoint`, triée, jamais vide, et dont le premier point est toujours à
l'origine.

| Décision | Raison |
|---|---|
| ancrage en **beats**, jamais en secondes | clips et notes sont en beats ; changer un tempo ne déplace donc aucun contenu musical, et il n'y a rien à remapper |
| **identité** et non position | `tempo.move` change la position d'un point ; un payload qui le désignerait par son beat viserait un autre point dès qu'une commande l'aurait déplacé |
| identifiant **constant** pour le point à l'origine | personne ne le crée, donc personne ne peut fournir son identifiant ; un ULID engendré rendrait deux projets vides différents |
| pas de champ `curve` | Tracktion en a un, c'est une forme d'automation, hors périmètre ; un champ que rien ne change ment. L'ajouter est additif |

Les bornes sont celles de Tracktion : `TempoSetting::minBPM` et `maxBPM`,
soit `[20, 300]`.

| Type | Ce qu'elle valide | Fusion |
|---|---|---|
| `tempo.insert` | ajout d'un changement, identifiant fourni par l'appelant | non |
| `tempo.remove` | retrait ; l'undoRecord porte le point entier, pas son identifiant | non |
| `tempo.set_bpm` | mutation continue, un tempo se tire comme un fader | par point |
| `tempo.move` | déplacement sur la timeline, sans toucher au bpm | par point |

Le point à l'origine refuse `tempo.remove` et `tempo.move` : la séquence doit
rester non vide et commencer à l'origine. Son bpm change comme celui de
n'importe quel autre point.

Deux points ne peuvent pas partager un beat, sinon le tempo en vigueur
dépendrait de l'ordre d'insertion, que le rejeu n'a aucune raison de
reproduire.

### Lire un projet écrit avant la séquence

`ProjectState::fromValue` accepte les deux formes : un `"tempo"` nombre vaut une
séquence d'un seul point à l'origine, et le projet obtenu est **égal** à un
projet construit aujourd'hui au même tempo. Aucun payload déjà écrit n'est
réécrit — aucun journal n'a jamais porté de commande de tempo, et c'est
précisément ce qui rendait la dette bon marché avant les clips audio.

### La preuve est une durée, pas un champ

Relire `edit.tempoSequence` prouverait qu'un nombre a été rangé là où on l'a
mis. Un tempo est une vitesse : ce qu'il change, c'est la durée de huit beats.
`core/engine/tests/TempoProjectionTests.cpp` mesure le rendu hors ligne.

| Séquence | Durée rendue |
|---|---|
| 8 beats à 120 BPM | 4 s |
| 8 beats à 240 BPM | 2 s |
| 4 beats à 120 puis 4 à 240 | 3 s |
| le même changement deux beats plus loin | 3,5 s |

Trois quarts et non la moitié : un point ne gouverne que les beats qui le
suivent. Une séquence projetée comme un tempo unique aurait rendu 2 s.

## Ce que le domaine ne lie pas

`core/domain` ne lie ni JUCE, ni Tracktion, ni `core/ui`. `cmake/DawGuards.cmake`
fait échouer le configure si c'est le cas, et `scripts/check_hygiene.py` refuse
les includes correspondants.

nlohmann/json est utilisé **uniquement** dans `src/serialization/Json.cpp`, lié
en `PRIVATE`, et n'apparaît dans aucun en-tête : le reste du projet ne l'hérite
pas, et il est remplaçable sans toucher une seule commande. Les commandes ne
manipulent que `daw::domain::Value`.


## Le modèle de pattern (S9)

Le contenu et le placement sont deux choses, et la séparation est ce qui rend
la playlist possible.

| Entité | Ce qu'elle porte | Ce qu'elle ne porte pas |
|---|---|---|
| `Clip` | une piste et ses notes. C'est la **ligne** qu'une piste joue dans un pattern | ni début ni longueur |
| `Pattern` | un nom, une longueur, au plus une ligne par piste | aucune position |
| `Placement` | un pattern et un beat | ni piste ni longueur |

Un pattern posé huit fois se modifie en **une** commande : les huit placements
ne portent aucune note. La projection, elle, pose un clip Tracktion par paire
(placement, ligne) — c'est le seul endroit où contenu et position se
rencontrent.

`clip.create_midi` garde exactement le payload qu'il a toujours eu
(`trackId`, `clipId`, `startBeats`, `lengthBeats`) et signifie désormais : un
pattern d'une ligne, plus un placement. Les identifiants du pattern et du
placement sont **dérivés des octets du clip**, jamais tirés — la règle « aucune
commande n'engendre d'identifiant » est intacte, et huit semaines de journaux
se rejouent sans une ligne de compatibilité.

`track.remove` emporte les lignes de la piste dans chaque pattern et les rend à
l'annulation ; son undoRecord porte désormais `rows`. Un enregistrement écrit
avant la S9 porte ses clips dans la piste et se relit comme un pattern d'une
ligne par clip.
