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
| 3 | **La preuve qui compte** : posé huit fois, modifié une fois, les huit changent | Livré, au rendu et dans l'application | §3.2 ; §8, point 5 |
| 4 | Le transport suit la playlist, les modes pattern et chanson coexistent | Livré | §3.3 |
| 5 | Projection par identité, sans reconstruire ce qui n'a pas changé, piège du tempo compris | Livré | §3.1, compteurs et identité d'objet |
| 6 | Commandes dans la table du copilote ; « répète le pattern 1 huit fois puis ajoute le pattern 2 » | Livré : une entrée copilote, un Ctrl+Z | §5 ; §8, point 7 |
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

C'est une page (F5) du beatmaker en fenêtres (§8 bis).

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

## 8. Les vérifications, faites par l'application

Tu m'as demandé de faire les vérifications moi-même. Tu as refusé deux fois que je prenne le
contrôle de l'écran, et je ne l'ai pas redemandé. C'est donc l'application qui les fait :
`--verify` rejoue la liste par les mêmes chemins qu'une personne. Les clics sont de vrais
événements souris envoyés au rack et à la playlist. PAT, SONG et « + Pattern » sont de vrais
boutons. Les Ctrl+Z passent par la vue, et la phrase est posée au vrai copilote. Chaque étape
laisse une capture de la fenêtre. Chaque étape qui change le son rend l'Edit hors ligne en WAV
et compte les attaques sur la grille du rack.

Résultat : **60 vérifications, aucune en échec**, sur trois exécutions : la liste (51), la
réouverture par un autre processus (5), un projet S8 + S9 (4). Les rapports et trois captures sont
dans `docs/verification-s10/`.

| # | Vérification | Mesuré |
|---|---|---|
| 1 | disposition | transport en barre ; Playlist, Channel rack, Historique et Copilote ouverts ; piano-roll fermé ; PAT allumé ; F7 ouvre le piano-roll, F7 le referme |
| 2 | « + Pattern » ne pose rien | 1 pattern, 0 pose, 1 entrée d'historique |
| 3 | le mode pattern joue sans pose | rendu de 8 s, 4 attaques aux pas 1, 5, 9, 13 |
| 4 | poser 8 fois, puis SONG | 8 poses aux mesures 1, 5 … 29 ; rendu de 64 s, 32 kicks ; la tête de lecture avance en lecture réelle |
| 5 | **la preuve** : 4 pas de hat allumés dans le rack | **64 attaques**, les 4 hats aux mêmes pas dans **chacune** des 8 poses ; 4 Ctrl+Z et on revient à 32 |
| 6 | glisser, retirer | le 4ᵉ bloc passe au temps 56, les 7 autres ne bougent pas, **une** entrée d'historique ; clic droit : bloc retiré, pattern gardé ; 2 Ctrl+Z : arrangement identique à l'octet près |
| 7 | copilote | **une** entrée marquée copilote ; pattern 2 posé une fois, après le pattern 1 ; aucun pattern créé ; **un** Ctrl+Z : état d'avant à l'octet près ; Ctrl+Y le refait |
| 8 | PAT isole le pattern 2 | rendu de 8 s, seulement ses 8 hats |
| 9 | tempo 120 → 90 | 72 s → 96 s (×4/3), chaque attaque sur son pas |
| 10 | renommer, supprimer | « Refrain » dans le sélecteur du rack ; supprimé avec ses poses ; Ctrl+Z le remet à sa place |
| 11 | fermer, rouvrir (autre processus) | même état, même profondeur d'historique (27), rouvert en PAT, même rendu (96 s, 40 attaques) |
| 12 | un projet S8 + S9 | 2 patterns, 2 poses ; rendu de 8 s, une attaque à chaque temps de 8 à 15, là où les clips commençaient |

**Ce que la vérification a trouvé.** Au premier passage, le copilote ne démarrait pas : il
cherchait `services/` à partir du **dossier courant**. Lancée ailleurs qu'à la racine du dépôt
(double-clic sur l'exécutable, par exemple), l'application lançait uv sur un dossier inexistant, et
le copilote mourait avec le code 2. Corrigé dans `2d7497d` : il cherche aussi en remontant depuis
l'exécutable. Le second passage a été lancé volontairement hors du dépôt, et le copilote a répondu.

**Ce que le copilote a répondu :** « Le pattern 1 était déjà répété huit fois (0 à 128). J'ai
ajouté le pattern 2 à la suite ». Huit poses existaient déjà, donc sa lecture est défendable. En
revanche il a écrit « à partir de la mesure 128 » alors qu'il s'agit du **temps** 128 (mesure 33).
C'est le prompt qu'il faut reprendre : lui faire donner les positions en mesures.

**Ce qui reste hors de portée d'une vérification automatique :**
- l'écoute sur les enceintes : le rendu hors ligne en est la mesure, pas l'expérience ;
- la boîte de dialogue « Renommer » de la playlist : elle est modale, donc le renommage a été
  vérifié par le bus ;
- le glissé d'une fenêtre avec la souris : placer et replacer les fenêtres est vérifié, mais pas
  le geste de la main.

Pour tout relancer :

```bash
"build/windows-msvc/core/app/daw_app_artefacts/Debug/DAW IA.exe" --project verif.dawproj --workspace beatmaker --verify verif
```

## 8 bis. Beatmaker en fenêtres, façon FL

Tu as jugé les panneaux découpés trop brouillons. Tu as validé des fenêtres internes, et c'est fait :

- une barre fixe (le transport), puis une rangée d'onglets, un par page ;
- chaque page est une fenêtre qu'on peut déplacer, redimensionner, agrandir (bouton ou double-clic
  sur le titre), fermer, ou mettre devant d'un clic ;
- **F5** playlist, **F6** channel rack, **F7** piano-roll, **F8** plugins, **F9** pistes. Chaque
  touche ouvre sa page, la met devant, ou la ferme si elle y est déjà ;
- la place des fenêtres est gardée en fractions de l'écran dans `DAW IA.layout`, jamais dans le
  projet ;
- un panneau dans une fenêtre ne répète plus son titre sous celui de la fenêtre ;
- les trois autres workspaces gardent leurs découpes pour l'instant.

L'architecture n'est pas rouverte : c'est toujours le manifeste qui dit quelles pages existent.
Il accepte simplement une mise en page `bar` + `pages`, validée par le schéma, par
`validate_workspaces.py` et par le lecteur de manifeste.

**Encore brouillon, et je préfère le dire :** chaque panneau garde sa bande d'en-tête, désormais
vide pour la playlist et le copilote. Et un pattern de 4 mesures affiche 64 pas minuscules dans le
rack. FL ouvre un pattern sur 1 mesure ; le passer à 1 mesure par défaut serait un choix à
valider.

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
| S10 | La playlist, puis le beatmaker en fenêtres | Livré ; 60 vérifications automatiques : §8 |

Aucune décision d'architecture n'a été rouverte. La règle des identifiants tient : chaque
`PlacementId` est tiré par l'appelant (la playlist, le copilote avec `$new:`), jamais par une
commande.
