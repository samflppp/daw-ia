# Bilan de fin de S9 — DAW IA

**Période :** semaine 9 sur 26. Rédigé le 22 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 4 commits (`d08564a` → `HEAD`).
**Volume :** 43 fichiers, +3 489 lignes, −328.
**Tests :** 211 cas hors audio (192 de domaine, 19 de persistance), 51 cas d'engine sous label
`audio`, 18 cas Python.
**Registry :** 33 types de commandes, 28 la semaine dernière.
**Schéma de projet :** 4, 3 la semaine dernière.

Un clip mélangeait ce qui se joue et où cela se joue. Il ne le mélange plus, et c'est tout ce que
cette semaine a fait — le channel rack est ce qui le rend visible.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Modèle de pattern exposé, options chiffrées, validé avant code | Livré | §2 ; trois options, recommandation, validation « option c » |
| 2 | Panneau `channel_rack` au manifeste, un canal par piste, grille, lecture, dessin | Livré | §6 ; `workspaces/beatmaker.json`, sept panneaux |
| 3 | Édition : clic pose et efface, glissé peint en un geste | Livré | §6.2 ; une entrée d'historique par trait |
| 4 | `track.set_channel_pitch` dans le domaine | Livré | §5 ; registry, table de descriptions, test de coalescence |
| 5 | Vélocité à la case, résolution de grille | Livré | §6.3 |
| 6 | Sélection du pattern courant partagée, boucle qui suit | Livré | §6.4 |
| 7 | Nouvelles commandes dans la table du copilote | Livré | §7 ; test croisé registry ↔ table dans les deux sens |
| 8 | Migration testée en processus enfant, fermer / rouvrir / mesurer | Livré | §4 ; 13 ms |
| 9 | **La preuve** : « mets un charleston en doubles-croches sur la piste 3 » | **Non vérifiée en vrai** | §8 — et je dis pourquoi plutôt que de l'affirmer |

## 2. La décision : le modèle de pattern

C'était la décision la plus structurante depuis la S2, parce qu'elle fige la forme des payloads
écrits dans des journaux réels. Trois options ont été exposées avant toute ligne de code.

| | a) lane renommée | b) pattern = regroupement de clips | **c) le clip devient le contenu** |
|---|---|---|---|
| Payloads de note à changer | 7 | 0 | **0** |
| Sens de commande redéfini | `clip.create_midi` | aucun | `clip.create_midi` |
| Projection à refaire | oui | non | oui |
| Modifier un pattern posé 8× | 1 commande | 8, et la 9ᵉ copie ne l'a pas | **1 commande** |
| Deux vérités pour « ce qui sonne » | non | oui dès qu'on synchronise | non |
| Ce que la S10 hérite | tout | rien d'utile | **tout** |

**Retenu : c.** C'est a) — la seule qui donne « modifier une fois, changer partout » — avec le
nommage qui fait que le journal des huit semaines passées se rejoue sans une ligne de
compatibilité.

### 2.1 La forme

| Entité | Ce qu'elle porte | Ce qu'elle ne porte pas |
|---|---|---|
| `Clip` | une piste et ses notes. C'est la **ligne** qu'une piste joue dans un pattern | ni début ni longueur |
| `Pattern` | un nom, une longueur, au plus une ligne par piste | aucune position |
| `Placement` | un pattern et un beat | ni piste ni longueur |

Le `Clip` garde son nom et son `ClipId` et perd son début et sa longueur. Le début remonte dans
`Placement`, la longueur dans `Pattern`. `Track::clips` disparaît.

C'est ce choix de nommage, et lui seul, qui fait que les sept verbes de note — `note.add`,
`note.remove`, `note.move`, `note.resize`, `note.set_velocity`, `note.quantize`, `note.transpose` —
ne changent pas d'un caractère.

### 2.2 Ce que `clip.create_midi` est devenu

Payload inchangé : `trackId`, `clipId`, `startBeats`, `lengthBeats`. Sens redéfini : un pattern
d'une ligne, long de `lengthBeats`, posé à `startBeats`.

Les deux identifiants qu'il ne reçoit pas — celui du pattern, celui du placement — sont **dérivés
des octets du clip**, avec un tag différent. Rien n'est engendré, le rejeu reste déterministe, et
la règle de la S2 tient telle quelle.

Un test épingle ce sur quoi tout repose : le payload porte exactement quatre clés. Une clé ajoutée
là casserait silencieusement tous les journaux d'avant, et c'est ce test qui le dirait.

### 2.3 Ce que ça a coûté ailleurs

| Pièce | Ce qui a changé |
|---|---|
| `track.remove` | emporte les lignes de la piste dans chaque pattern ; son undoRecord porte `rows`. Un enregistrement écrit avant la S9 porte ses clips dans la piste et se relit comme un pattern d'une ligne par clip |
| `StateView::summarise` | les clips quittent les pistes et deviennent `patterns[].clips[]`, avec les placements |
| `PianoRollPanel` | lit la ligne de la piste sélectionnée dans le pattern courant, et perd son sélecteur de clip |
| Projection | un clip Tracktion par paire (placement, ligne) |

Trois fichiers de vue et un projecteur. Pas de dette de lecture double, nulle part.

## 3. La projection

Le projecteur marche les patterns et les placements ensemble. C'est le seul endroit où contenu et
position se rencontrent, et c'est une projection, pas un état : le domaine tient la note une fois,
quel que soit le nombre de placements.

Deux choses ont dû bouger et une seule était prévisible :

- **l'instantané de saut** n'est plus la piste seule mais `{ piste, joue }`. Sans cela une note
  ajoutée à un pattern ne rebâtissait rien, parce que la forme sérialisée de la piste n'avait pas
  changé — et l'Edit restait muet. Un glissement de fader ne rebâtit toujours rien ;
- **le nom du clip Tracktion** porte les deux identifiants (`placementId:clipId`). La même ligne
  posée deux fois est deux clips, et deux clips du même nom seraient deux choses que rien ne
  distingue.

Cinq cas d'engine neufs le prouvent contre Tracktion, dont celui-ci : un pattern posé huit fois,
une `note.add`, et les huit clips l'entendent. Un Ctrl+Z la retire des huit, sans une ligne de code
moteur.

## 4. La migration, mesurée

Un scénario enfant écrit un projet avec les payloads d'un build des huit premières semaines, puis
remet `schema_version` à 3. Le parent ouvre : la migration 3 → 4 tourne sur un fichier qui est
vraiment en retard.

| Mesure | Valeur |
|---|---|
| Réouverture + migration 3 → 4 | **13 ms** pour 6 lignes de journal |
| Version sur disque après ouverture | 4 |
| Pattern reconstruit | 1, d'une ligne, 4 notes |
| Placement | 1, au beat 8 — là où le clip commençait |
| Profondeur d'annulation rendue | 6 (piste, clip, quatre notes) |

**Le schéma 4 ne convertit rien.** Aucune colonne ne bouge : le journal stocke des payloads, et les
payloads n'ont pas changé. Le numéro existe pour l'autre sens — un projet où un pattern est posé
deux fois se rejouerait dans un build de la S8 comme deux patterns qu'il ne sait pas distinguer, et
rien dans un payload ne le dirait. La version est ce qui fait refuser ce build au lieu de le
laisser mentir.

**Ce que ce test ne prouve pas, et je préfère l'écrire :** il ne lance pas un binaire de la S8. Il
écrit les mêmes octets avec les commandes d'aujourd'hui, parce qu'aucune des quatre n'a changé de
payload — et c'est précisément ce fait-là qui est épinglé par le test du §2.2. Si ce fait tombe,
les deux tests tombent ensemble, ce qui est la propriété que je voulais.

## 5. `track.set_channel_pitch`

La S7bis l'avait nommée comme le seul ajout vraiment justifié au domaine, et la raison tient
encore : une hauteur connue d'un seul écran est une hauteur qu'un copilote ne connaît pas.

| Point | Choix |
|---|---|
| où elle vit | sur la `Track`, pas dans le panneau |
| ce qu'elle fait | elle est lue quand une case s'allume |
| ce qu'elle ne fait pas | elle ne réécrit aucune note existante — sinon changer la hauteur d'un canal réécrirait silencieusement une ligne de basse |
| coalescence | par piste : traverser une douzaine de demi-tons est une entrée |
| absente d'un projet ancien | vaut 60, ce qui est exactement là où tous étaient, puisque rien ne la lisait |

## 6. Le channel rack

### 6.1 Ce qu'une case est

Une note. Dans la ligne que la piste joue dans le pattern courant, à la hauteur du canal, longue
d'un pas. Le point de la S7bis §10 tient intégralement : le rack et le piano-roll sont deux vues du
même vecteur, donc il n'y a rien à accorder entre eux.

### 6.2 Le trait

Le **clic décide de ce que fait tout le glissé**. Un trait qui commence en allumant allume tout ce
qu'il traverse ; un trait qui commence en effaçant efface. Un glissé qui aurait basculé chaque case
aurait effacé au retour ce qu'il venait de dessiner.

Un geste, une entrée d'historique. Traverser deux fois la même case n'écrit qu'une fois : sans
cela, une main lente envoyait une commande par mouvement de souris.

Allumer un pas sur un canal qui n'a pas encore de ligne dans ce pattern **ouvre la ligne dans le
même groupe**. Une ligne laissée derrière par une note annulée est une ligne que personne n'a
demandée.

### 6.3 Vélocité et résolution

La vélocité est la teinte de la case, exactement comme au piano-roll : la même valeur lue de la
même façon dans les deux vues de la même note.

La résolution — noires, croches, doubles, triples — est un **réglage d'écran**. Elle écrit aucune
commande et ne déplace aucune note : elle décide de la finesse du découpage, jamais de la longueur.
Un pattern de 4 temps en doubles montre 16 pas ; le même en croches en montre 8. **Le pattern reste
la vérité**, comme recommandé en S7bis §10.4.

### 6.4 Le pattern courant

Il vit dans `Selection`, à côté de la piste sélectionnée, et pour la même raison : ce n'est pas une
propriété du projet. Deux fenêtres sur le même projet regarderaient chacune le leur, et annuler une
note ne doit pas déplacer le pattern sous l'utilisateur.

Le rack le choisit, le piano-roll suit, la boucle suit. **Un seul sélecteur** pour un seul choix :
deux auraient été exactement la deuxième vérité que la S7bis a refusée.

## 7. Les cinq commandes neuves

| Commande | Pourquoi elle existe |
|---|---|
| `pattern.create` | un pattern vide, d'une longueur donnée. Vide et non « une ligne par piste » : un pattern qui se remplirait tout seul voudrait dire autre chose selon le moment où il tourne |
| `pattern.place` | pose un pattern sur la timeline. Le rack en a besoin pour qu'un pattern neuf sonne ; c'est un verbe, pas la playlist |
| `pattern.add_track` | ouvre la ligne d'une piste. Vide, pour que `note.add` continue à ne vouloir dire qu'une chose |
| `pattern.set_length` | la longueur ; les notes au-delà sont **gardées**, jamais coupées — une commande qui les détruirait ne saurait pas les rendre |
| `track.set_channel_pitch` | §5 |

Restées dehors et c'est volontaire : `pattern.remove`, `pattern.rename`, `placement.move`,
`placement.remove`. Aucun écran de cette semaine ne peut les appeler, et un verbe qu'aucun écran
n'appelle est un verbe que rien ne prouve. Elles sont la S10.

Registry 28 → 33. Le test croisé registry ↔ table de descriptions passe dans les deux sens : une
commande sans description échoue, une description orpheline échoue aussi.

## 8. La preuve, et pourquoi elle n'est pas faite

« mets un charleston en doubles-croches sur la piste 3 » n'a **pas** été passée au modèle. La
sortie réseau est bloquée depuis l'environnement où j'ai travaillé : la requête vers l'API expire.
Je préfère l'écrire que de raconter un résultat.

Ce qui est vérifié sans le modèle :

- les cinq commandes sont dans le registry et dans la table de descriptions, testé dans les deux
  sens ;
- le prompt système apprend le modèle : le contenu vit dans des patterns, une note s'écrit dans une
  ligne, et **un pattern posé huit fois se modifie une seule fois** — avec l'interdiction explicite
  d'écrire la même note plusieurs fois pour couvrir plusieurs placements ;
- la frappe des identifiants `$new:` marche sans une ligne neuve : `resolve_new_ids` remplace
  n'importe quelle chaîne du payload, donc `patternId` et `placementId` passent comme le reste ;
- 18 cas Python verts.

Ce qui reste à faire, par toi, est le point 8 du §10.

## 9. Ce que la S10 hérite

| Pièce | Ce que la playlist en fait |
|---|---|
| `Placement` | c'est **tout** ce qu'elle manipule. Poser, déplacer, retirer |
| `ProjectState::placementsOf` | les placements d'un pattern, triés par beat. La playlist les lit tous, la boucle beatmaker lit le premier |
| `ProjectProjector::playedValue` | déjà écrit contre les placements : ajouter une ligne de playlist ne demande rien au projecteur |
| Les quatre verbes restants | `pattern.place` existe déjà ; restent `placement.move`, `placement.remove`, `pattern.remove`, `pattern.rename` |
| Le schéma | 4 suffit. Un placement de plus est une ligne de journal de plus, pas une colonne |

Ce qu'elle **n'hérite pas** et qu'il faudra trancher : la notion de ligne de playlist. Un
`Placement` ne porte pas de piste, délibérément — un pattern sait déjà sur quelles pistes ses
lignes sonnent. Si la playlist veut des rangées comme FL, ce sera une décision d'écran, ou un champ
neuf, et c'est à exposer avant de coder.

## 10. Tes vérifications, visuelles et auditives, dans l'ordre

Projet bac à sable, jamais le tien. Construis puis lance :

```bash
cmake --build --preset windows-msvc
```

1. **Le rack existe et occupe sa place.** Workspace beatmaker : sept panneaux, le CHANNEL RACK
   au-dessus du PIANO-ROLL, et à sa droite CHAÎNE / HISTORIQUE / COPILOTE. Sur un projet vide le
   rack dit « ajoutez une piste ».

2. **Un pattern naît en une entrée.** Ajoute trois pistes (Kick, Snare, Charleston). Clique
   **+ Pattern**. L'historique gagne **une** ligne marquée `x2` — le pattern et son placement. Le
   sélecteur affiche « Pattern 1 ». Ctrl+Z les retire ensemble, Ctrl+Y les ramène ensemble.

3. **Une case allumée est une note, et ça s'entend.** Mets un instrument sur la piste Kick. Clique
   les pas 1, 5, 9, 13 de sa ligne. Lance la lecture : **quatre kicks par mesure, en boucle**. La
   colonne sous la tête de lecture s'éclaire pas à pas.

4. **Le rack et le piano-roll sont la même chose.** Sans rien toucher d'autre, regarde le
   piano-roll : les quatre notes y sont, à la hauteur du canal, à la même place. Déplace-en une
   d'un demi-ton au piano-roll — **la case correspondante ne bouge pas de colonne dans le rack**,
   parce que c'est la même note et qu'elle a changé de hauteur, pas de pas.

5. **Le glissé est un geste.** Sur la ligne du Charleston, appuie sur le pas 1 et traîne jusqu'au
   pas 16 sans relâcher. Seize cases s'allument. L'historique gagne **une seule** ligne. Un Ctrl+Z
   efface les seize d'un coup. Recommence en partant d'une case allumée : le trait **efface** au
   lieu d'allumer.

6. **La hauteur du canal.** Clic droit sur le nom « Kick » dans la colonne de gauche : un menu de
   deux octaves. Choisis C1. Le libellé à droite du nom passe à `C1`, l'historique gagne une ligne,
   **et les kicks déjà posés ne changent pas de son** — c'est voulu, et c'est ce qu'il faut
   vérifier. Allume un pas neuf : *celui-là* sonne en C1.

7. **La grille suit le pattern.** Change la résolution de « doubles » à « croches » : le nombre de
   colonnes est divisé par deux, **et aucune note ne bouge, et l'historique ne gagne rien**. Repasse
   en doubles : tout revient où c'était.

8. **La preuve copilote** — celle que je n'ai pas pu faire. Avec `DAW_IA_ANTHROPIC_API_KEY` posée,
   tape dans le copilote : `mets un charleston en doubles-croches sur la piste 3`. Attendu : la
   ligne du Charleston se remplit à l'écran, **une** entrée d'historique marquée « copilote » et
   portant ta phrase, et **un** Ctrl+Z qui vide toute la ligne. Si le copilote écrit note par note
   sans grouper, ou s'il crée un deuxième pattern, dis-le-moi : c'est le prompt qu'il faut reprendre,
   pas le modèle de données.

9. **Le pattern posé deux fois se modifie une fois.** Demande au copilote : `pose le pattern
   courant une deuxième fois juste après`. Puis allume un pas neuf dans le rack. Lance la lecture
   sur toute la timeline (coupe la boucle) : **le pas neuf s'entend aux deux endroits**. C'est la
   démonstration de toute la semaine.

10. **Ça survit à une fermeture.** Ferme l'application, rouvre le projet. Le rack montre le même
    pattern, les mêmes cases, les mêmes teintes de vélocité, et l'historique a la même profondeur.

11. **Un vieux projet se relit.** Ouvre un projet d'une semaine précédente. Attendu : chaque clip
    d'alors est devenu un pattern d'une ligne, posé à la mesure où il commençait ; le rack les
    montre un par un dans son sélecteur, et **le morceau sonne exactement comme avant**. C'est le
    point le plus important de cette liste.

## 11. Ce qui n'a pas été fait

| Point | Pourquoi |
|---|---|
| La preuve copilote en vrai | réseau bloqué ici ; §8 |
| Le nom d'un pattern, modifiable | `pattern.rename` est en S10 avec les autres verbes de placement. Un pattern sans nom s'affiche par son rang, et le rang est une lecture, pas un état |
| Réordonner les canaux du rack | c'est `track.reorder`, qui existe déjà et que le rack n'expose pas encore. Rien ne bloque |
| Un pas qui dure plus d'un pas | le rack dessine où un son commence ; sa durée est ce que le piano-roll est |
| Deux notes du même canal sur le même pas | refusé par construction : une case tient une note. Deux seraient un son et deux choses à cliquer |

## 12. Périmètre

Hors périmètre et resté dehors : la playlist elle-même, moteur génératif, moteur harmonique,
automation, branches d'arrangement, les trois autres workspaces, duplication, optimisation du
contexte du copilote.

Une décision de la S8 est explicitement levée : le bilan de la S8 §15 laissait ouverte la question
« dans FL un pattern regroupe plusieurs canaux, chez nous un clip appartient à une seule piste ».
Elle est tranchée, et dans le sens de FL.

## 13. Suivi d'avancement

| S | Visait | État aujourd'hui |
|---|---|---|
| S1 | Socle : outillage, structure, CI | Acquis |
| S2 | Command Bus | Acquis |
| S3 | Le bus pilote un vrai moteur audio | Acquis |
| S4 | Hébergement VST3 et CLAP | Acquis |
| S5 | Persistance et provenance | Acquis |
| S6 | Une interface qui se montre | Acquis |
| S7 | Le vocabulaire du domaine | Acquis |
| S8 | Le copilote | Acquis |
| S9 | Le channel rack, sur un modèle de pattern | — |

Aucune décision d'architecture n'a été rouverte. La règle d'identifiant de la S2 a été le point dur
de la semaine et elle n'a pas été assouplie : `clip.create_midi` avait besoin de deux identifiants
que son payload ne porte pas, et la réponse a été de les **dériver** de celui qu'il porte, pas de
les tirer.
