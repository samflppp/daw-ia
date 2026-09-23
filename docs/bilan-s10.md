# Bilan de fin de S10 — DAW IA

**Période :** semaine 10 sur 26. Rédigé le 23 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 10 commits S10 (`295d36d` → `HEAD`).
**Volume :** 37 fichiers, +2 657 lignes, −230 (IDEES.md compris).
**Tests :** 222 cas hors audio (202 de domaine, 20 de persistance), 58 cas d'engine sous label
`audio`, 18 cas Python.
**Registry :** 38 types de commandes, contre 33 la semaine dernière.
**Schéma de projet :** 4, inchangé.

La S9 avait séparé le contenu de sa position. Cette semaine, la position est devenue un écran : la
playlist. Et l'affirmation « modifié une fois, change partout » est maintenant prouvée par un rendu
audio, plus seulement par une lecture de l'état.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 0 | Modèle de placement et bascule pattern/chanson exposés et validés avant de coder | Livré | les quatre points validés : une ligne par pattern, pas de redimensionnement, `transport.set_mode`, quatre verbes |
| 1 | Panneau `playlist` déclaré dans `beatmaker.json` | Livré | §4 ; huit panneaux |
| 2 | Poser, déplacer, supprimer, par le bus, annulables, identifiants fournis par l'appelant | Livré | §2 ; pas de redimensionnement, par décision |
| 3 | **La preuve qui compte** : posé huit fois, modifié une fois, les huit changent | Livré au rendu ; **l'écran reste à vérifier** | §3.2 ; §8, points 3 à 5 |
| 4 | Le transport suit la playlist, les modes pattern et chanson coexistent | Livré | §3.3 |
| 5 | Projection par identité, sans reconstruire ce qui n'a pas changé, piège du tempo compris | Livré | §3.1, compteurs et identité d'objet |
| 6 | Commandes dans la table du copilote ; « répète le pattern 1 huit fois puis ajoute le pattern 2 » | Livré côté modèle ; **écran et Ctrl+Z non vérifiés** | §5 |
| 7 | Les projets des semaines précédentes se relisent | Livré | §6 ; trois processus, 22 ms puis 28 ms |

## 2. Le modèle de placement retenu

Il n'a rien changé au modèle de la S9, et c'était l'objectif.

| Question | Réponse | Pourquoi |
|---|---|---|
| Une ligne de playlist par quoi ? | **par pattern**, déduite de l'ordre des patterns | un `Placement` ne porte pas de piste. Avec une ligne par piste, glisser un bloc déplacerait aussi le pattern sur toutes les autres pistes |
| Où sont stockées les lignes ? | nulle part | la ligne n = le n-ième pattern. Aucun état d'écran dans le domaine, aucun champ `lane` |
| Redimensionner une pose ? | non | la longueur appartient au pattern. Étirer une pose sur huit ne doit pas étirer les sept autres. Une pose tronquée serait un `lengthBeats` optionnel sur le `Placement` : un ajout sans migration, reporté |

Les quatre commandes neuves :

| Commande | undoRecord | Coalescence |
|---|---|---|
| `placement.move` | l'ancien temps | par placement, sur le geste : un glissé = une entrée |
| `placement.remove` | le placement **et son rang** dans l'arrangement | — |
| `pattern.rename` | l'ancien nom | — |
| `pattern.remove` | le pattern, son rang, ses lignes, **chaque placement avec son rang** | — |

Les rangs sont conservés pour qu'une annulation rende *le même* arrangement, pas seulement un
arrangement équivalent. Deux ordres différents donnent deux formes sérialisées différentes. Un
test vérifie l'égalité à l'octet près après l'annulation. `ProjectState` a gagné `placementIndex`
et `insertPlacement`.

## 3. Moteur et transport

### 3.1 Réconciliation par identité

Avant, une note ajoutée effaçait puis reposait **tous** les clips de la piste. Maintenant, chaque
clip Tracktion porte une clé (`placement:ligne`, ou `audition:ligne` en mode pattern). Le
projecteur ne touche un clip que sur ce qui a changé :

| Changement | Effet dans l'Edit | Mesuré par |
|---|---|---|
| déplacer une pose parmi huit | 1 clip repositionné, 0 inséré, 0 réécrit, **les huit mêmes objets** (`EditItemID`) | compteurs `stats()` et identités |
| une note ajoutée à un pattern posé 8 fois | 8 séquences réécrites, 0 insertion, 0 déplacement | compteurs |
| **tempo 120 → 60** (le piège de la S7) | 4 clips replacés en secondes, 0 séquence réécrite ; le beat 12 passe de 6 s à 12 s | compteurs et positions |
| déplacer un fader | aucun clip touché | l'instantané de piste est séparé de l'instantané de ce qu'elle joue |

La boucle a eu le même traitement. Elle est recalculée à chaque projection et replacée quand le
tempo bouge. Avant, un changement de tempo laissait la boucle sur les anciennes secondes : c'était
le même piège, à un autre endroit.

### 3.2 La preuve au rendu

`ArrangementTests.cpp` fait un rendu hors ligne de l'Edit, puis écoute le premier quart de chaque
temps :

| Situation | Durée du rendu | Temps qui sonnent |
|---|---|---|
| un pattern de 4 temps, un coup sur le temps 1, posé 8 fois | **16 s** | **8** (temps 0, 4 … 28) |
| une seule `note.add` au temps 3 **du pattern** | 16 s | **16** (les 8 d'avant + 2, 6 … 30) |
| un Ctrl+Z | 16 s | 8 |
| mode pattern sur un autre pattern | **2 s** | 1, seul, en boucle 0 → 2 s |

### 3.3 Mode pattern et mode chanson

Comme validé :

- `transport.set_mode {mode, patternId}` est une commande **transitoire** : ni historique, ni
  journal. Le mode vit dans `TransportState`, comme la tête de lecture. Le pattern est dans le
  payload, jamais lu sur un écran, donc le copilote peut basculer lui aussi.
- **Mode pattern :** l'Edit ne contient que le pattern auditionné, au temps 0, et boucle sur sa
  longueur. Si le pattern s'allonge, la boucle s'allonge sans aucune commande de transport.
- **Mode chanson :** tous les placements, et la boucle de `transport.set_loop` s'il y en a une.
- Changer de mode ramène la tête de lecture au début : le temps 40 de la chanson n'existe pas
  dans un pattern de 4 temps.
- Le transport a deux boutons, **PAT** et **SONG**. L'application s'ouvre en mode pattern sur le
  premier pattern.
- Le rack et le piano-roll suivent le pattern courant (`patternEditing::follow`). Le rack
  resynchronise l'audition **en différé**, hors de la notification du bus, quand une annulation ou
  un `pattern.remove` retire le pattern écouté.

**Un changement de comportement de la S9, dit plutôt qu'appliqué en douce :** « + Pattern » ne
pose plus le pattern sur la timeline. En S9 c'était nécessaire, parce que la boucle avait besoin
d'une pose pour faire entendre le pattern. Le mode pattern joue le pattern là où il est, donc
chaque nouveau brouillon n'encombre plus la chanson. C'est ce que fait FL. Aucune architecture
n'est touchée.

## 4. La playlist à l'écran

Elle est placée au-dessus du channel rack et du piano-roll, dans la colonne centrale.

| Geste | Commande |
|---|---|
| clic dans la ligne d'un pattern | `pattern.place` à la mesure sous le pointeur (Maj : au temps) |
| glisser un bloc | `placement.move`, un geste, une entrée d'historique |
| clic droit sur un bloc | `placement.remove` |
| clic sur l'en-tête de ligne | sélectionne le pattern ; le rack, le piano-roll et le mode pattern suivent |
| clic droit ou double-clic sur l'en-tête | renommer ; supprimer le pattern |
| clic dans la règle | déplace la tête, en mode chanson |

Les poses du pattern courant sont en couleur pleine et les autres en couleur atténuée. Quand on
modifie le rack, on voit donc d'avance tous les endroits qui vont changer. La timeline montre au
moins 16 mesures, plus 4 mesures libres après la dernière pose.

## 5. Le copilote

- Les 5 nouvelles commandes sont dans la table. Le test croisé registry ↔ table passe dans les
  deux sens (38 = 38).
- L'état résumé donne à chaque pattern son **rang** (`rank`) et le **libellé** affiché (`label`).
  Ainsi « le pattern 2 » désigne la même chose pour l'utilisateur et pour le modèle. Il donne
  aussi `arrangementEndBeats`, la fin du morceau, et le mode de lecture.
- Le prompt apprend qu'on répète un pattern avec N `pattern.place` bout à bout, **jamais** en
  créant un nouveau pattern.

**Essai en vrai, hors CI** (vrai Claude Sonnet, faux DAW servant l'état et les schémas de cette
semaine). « répète le pattern 1 huit fois puis ajoute le pattern 2 » donne **un seul groupe** de
8 `pattern.place` : le pattern 1 aux temps 16, 32 … 112, puis le pattern 2 à 128. La pose
existante au temps 0 compte comme la première. 4 967 tokens en entrée, 1 412 en sortie.

**Ce que cet essai ne prouve pas :** l'effet à l'écran, et le fait qu'un seul Ctrl+Z défait tout.
Côté domaine, un groupe = une entrée d'historique : c'est acquis depuis la S8, et le test de
migration (§6) le vérifie sur un groupe `origin=copilot` relu depuis le disque. Le point 7 du §8
reste à faire par toi.

## 6. Migration

**Il n'y a pas de schéma 5.** La playlist n'ajoute aucun champ, seulement des types de commande.
Un build plus ancien les refuse de lui-même : son registry ne les connaît pas.

Le test fait tourner trois processus :

1. un enfant écrit un projet S8 (`clip.create_midi`), sur le schéma 3 ;
2. un autre enfant l'ouvre, le migre vers le schéma 4, et y ajoute ce que le rack de la S9
   écrivait (pattern + pose en un groupe, une ligne ouverte par la première case, un trait) ;
3. le parent (S10) rouvre le projet : **22 ms pour 13 lignes**. Il applique les verbes de la
   playlist (7 poses dans un groupe copilote, un déplacement, un renommage, un retrait), puis
   ferme ;
4. un enfant rouvre le projet et rend son état. Le parent rouvre à son tour : **28 ms pour
   23 lignes**. Les deux états sont identiques, la profondeur d'historique aussi, et **quatre
   Ctrl+Z rendent le projet de la S9 à l'octet près**.

**Limite, comme en S9 :** aucun binaire S8 ou S9 n'est réellement lancé. Les enfants écrivent
avec les commandes d'aujourd'hui, dont les payloads n'ont pas changé ; le test épinglé en S9 sur
`clip.create_midi` le garantit.

## 7. Ce que je n'ai pas fait, et pourquoi

| Point | Pourquoi |
|---|---|
| Vérifier à l'écran | j'ai demandé l'accès à l'application et il a été refusé ; je n'ai pas réessayé. En plus, une instance S8 (`C:\dawS9-bin\s8`, lancée à 06:56) tournait déjà : ma propre instance s'est fermée tout de suite, et je n'ai pas arrêté un processus qui n'était pas le mien |
| Le Ctrl+Z copilote à l'écran | même raison ; §5 |
| Redimensionner une pose | décision validée ; §2 |
| Défilement et zoom de la playlist | la timeline s'adapte à la largeur. Au-delà d'une soixantaine de mesures, les blocs deviennent étroits |
| Lignes libres façon FL (plusieurs patterns sur une ligne) | écarté à la validation : ce serait un état d'écran dans le domaine |

**IDEES.md :** aucune idée du fichier n'a servi cette semaine, et rien n'y a été mis en œuvre.

## 7 bis. La CI est passée au rouge, et la dette de la S9 est payée

`b7df698` (la playlist) et `6b7a37a` (la migration) ont été poussés avec une violation de
`check_hygiene.py` : `.reduced(0, …)` dans `PlaylistPanel.cpp`. Même un littéral d'espacement nul
est interdit dans un panneau. J'avais lancé clang-format, ruff et pytest, mais pas l'hygiène :
**exactement la dette que le bilan S9 nommait.**

- corrigé dans le commit suivant, avec `withTrimmedTop` / `withTrimmedBottom` sur le jeton ;
- `scripts/check-all.sh` existe maintenant. Il lance, dans l'ordre de la CI : clang-format,
  hygiène, manifestes, ruff, pytest, et s'arrête au premier échec. Il ne compile rien : ça reste
  le rôle des presets.

## 8. Tes vérifications, dans l'ordre

Sur un projet bac à sable. **Ferme d'abord l'instance S8 qui tourne** : l'application n'accepte
qu'une seule instance à la fois.

```bash
cmake --build --preset windows-msvc
```

1. **La disposition.** Workspace beatmaker : 8 panneaux. Au centre, de haut en bas : PLAYLIST,
   CHANNEL RACK, PIANO-ROLL. Dans le transport, après la signature : **PAT** allumé, SONG éteint.
2. **« + Pattern » ne pose plus rien.** Ajoute deux pistes (Kick, Hat) et mets un instrument sur
   chacune. Clique + Pattern. La playlist montre une ligne « Pattern 1 », **vide**. L'historique
   gagne une ligne.
3. **Le mode pattern s'entend sans pose.** Allume les pas 1, 5, 9, 13 du Kick et lance la lecture :
   quatre kicks en boucle, alors que la playlist est vide. **Stop.**
4. **Poser huit fois.** Clique 8 fois dans la ligne Pattern 1, aux mesures 1, 5, 9 … 29 (le
   pattern dure 4 mesures). Huit blocs en couleur pleine. Passe en **SONG**, puis lecture : les
   kicks durent 32 mesures et la tête de lecture traverse la playlist.
5. **LA preuve.** En SONG, pendant la lecture, allume dans le rack les pas 3, 7, 11, 15 du Hat.
   **Les huit blocs le jouent.** À l'oreille : le hat arrive dès la pose suivante, et partout.
   Ctrl+Z : il disparaît des huit d'un coup.
6. **Glisser, retirer.** Glisse le 4ᵉ bloc vers la droite : il suit à la mesure près, les autres
   ne bougent pas, **une seule** ligne d'historique. Clic droit sur un bloc : il disparaît, le
   pattern reste. Ctrl+Z deux fois : tout revient à sa place.
7. **La preuve copilote.** Crée un 2ᵉ pattern (+ Pattern), allume quelques pas. Dans le copilote :
   `répète le pattern 1 huit fois puis ajoute le pattern 2`. Attendu : les blocs apparaissent dans
   la playlist, **une** entrée « copilote » dans l'historique, et **un** Ctrl+Z qui les retire
   tous. Il est normal que la pose existante compte comme la première (§5).
8. **PAT isole.** Sélectionne Pattern 2 dans le rack, puis PAT et lecture : **seul** le pattern 2
   sonne, en boucle, même s'il chevauche le pattern 1 dans la chanson. Repasse en SONG : tout
   l'arrangement revient.
9. **Le tempo.** En SONG, passe le tempo de 120 à 90 pendant la lecture : les blocs gardent leur
   place en mesures et le morceau ralentit sans décalage entre les pistes.
10. **Renommer, supprimer.** Double-clic sur l'en-tête « Pattern 2 », tape « Refrain » : le nom
    change dans la playlist **et** dans le sélecteur du rack. Clic droit, puis Supprimer : la
    ligne et ses blocs disparaissent. Ctrl+Z : ils reviennent au même endroit.
11. **Ça survit à une fermeture.** Ferme, puis rouvre : même playlist, mêmes noms, même
    historique, ouverture en PAT sur le premier pattern.
12. **Un vieux projet se relit.** Ouvre un projet de la S8 ou de la S9 : chaque ancien clip est
    une ligne de playlist avec son bloc là où il commençait, et en SONG **le morceau sonne comme
    avant**.

## 9. Ce que la S11 hérite

| Pièce | Usage |
|---|---|
| `ProjectProjector::stats()` | tout ajout au moteur peut prouver qu'il ne reconstruit pas plus que nécessaire |
| `transport.set_mode` | le point d'entrée pour faire écouter un pattern généré avant de le poser |
| `arrangementEndBeats`, `rank`, `label` | le vocabulaire d'arrangement du copilote |
| Pose tronquée, défilement de la playlist | reportés, sans migration |

## 10. Suivi d'avancement

| S | Visait | État |
|---|---|---|
| S1–S8 | Socle, bus, moteur, plugins, persistance, UI, domaine, copilote | Acquis |
| S9 | Channel rack, modèle de pattern | Acquis |
| S10 | La playlist | Livré ; vérification écran et oreille : §8 |

Aucune décision d'architecture n'a été rouverte. La règle des identifiants tient : chaque
`PlacementId` est tiré par l'appelant (la playlist, le copilote avec `$new:`), jamais par une
commande.
