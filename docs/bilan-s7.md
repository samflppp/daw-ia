# Bilan de fin de S7 — DAW IA

**Période :** semaine 7 sur 26. Rédigé le 21 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 11 commits (`16ddfa1` → `b348995`).
**Volume :** 30 fichiers, +3 253 lignes, −69.
**Tests :** 175 cas hors audio (159 de domaine, 16 de persistance), 42 cas d'engine sous label `audio`.
**Registry :** 27 types de commandes, contre 17 à la fin de la S6.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Tempo en séquence, commandes annulables, projection, migration | Livré | 4 commandes ; durées rendues mesurées, §3 |
| 2 | Pan par piste, loi exposée, contrôle dans le panneau | Livré | `PanLaw3dBCenter` écrite explicitement ; RMS par canal, §4 |
| 2b | Largeur stéréo | **Écartée, et dit** | Tracktion n'a pas de plugin de largeur ; `width` n'existe que dans le Chorus et le Reverb |
| 3 | Les verbes manquants | Livré jusqu'à la ligne de coupe validée | 10 commandes neuves ; duplication coupée, §5 |
| 4 | Chaque commande utilisable par un appelant qui ne voit que son type et son schéma | Livré | aucune commande ne lit une sélection ; §6 |

## 2. La décision de la semaine : nommer un point plutôt que le situer

Le tempo aurait pu être une liste de paires `(beat, bpm)` désignées par leur
position. Il a fallu trente secondes pour voir que non : `tempo.move` déplace un
point, donc un payload qui désignerait un point par son beat viserait **un autre
point** dès qu'une commande l'aurait déplacé. Chaque point porte un
`TempoPointId`, septième type d'identifiant du projet.

La conséquence a été plus intéressante que la décision. Le point à l'origine
existe dans tout projet par construction : personne ne le crée, donc personne ne
peut fournir son identifiant, et un ULID engendré à la construction rendrait
**deux projets vides différents** — `operator==` compare les séquences, et toute
la suite de tests compare des états. Son identifiant est donc une constante,
égale dans tout projet, sur toute machine.

C'est la même règle qu'en S3 et en S4, appliquée une fois de plus : ce que le
projet n'engendre pas, il ne l'invente pas.

## 3. Le tempo, et ce que la preuve mesure

| Décision | Raison |
|---|---|
| ancrage en **beats** | clips et notes sont en beats ; changer un tempo ne déplace aucun contenu musical, et il n'y a rien à remapper |
| **identité**, jamais position | voir §2 |
| pas de champ `curve` | Tracktion en a un, c'est une forme d'automation, hors périmètre ; un champ que rien ne change ment |

Bornes : `TempoSetting::minBPM` et `maxBPM`, soit `[20, 300]` — celles de
Tracktion, comme les bornes de volume en S3.

| Commande | Fusion |
|---|---|
| `tempo.insert` | non |
| `tempo.remove` | non |
| `tempo.set_bpm` | par point |
| `tempo.move` | par point |

Le point à l'origine refuse `tempo.remove` et `tempo.move` : la séquence doit
rester non vide et commencer à l'origine. Son bpm change comme celui des autres.

**La migration a coûté une branche de lecture, et c'est tout.**
`ProjectState::fromValue` accepte un `"tempo"` nombre comme une séquence d'un
seul point, et le projet obtenu est **égal** à un projet construit aujourd'hui au
même tempo. Aucun payload déjà écrit n'est réécrit : aucun journal n'a jamais
porté de commande de tempo. C'est exactement la dette que la S6 disait de payer
tôt, et le prix a été celui annoncé.

**La preuve est une durée, pas un champ.** Relire `edit.tempoSequence`
prouverait qu'un nombre a été rangé là où on l'a mis.

| Séquence | Durée rendue hors ligne |
|---|---|
| 8 beats à 120 BPM | 4 s |
| 8 beats à 240 BPM | 2 s |
| 4 beats à 120 puis 4 à 240 | 3 s |
| le même changement deux beats plus loin | 3,5 s |

Trois quarts et non la moitié : un point ne gouverne que les beats qui le
suivent. Une séquence projetée comme un tempo unique aurait rendu 2 s, et le
test serait rouge.

**Côté projection**, un effet de bord a failli passer inaperçu : un clip est
inséré dans l'Edit comme un **intervalle de temps**, calculé depuis les beats.
Les mêmes beats tombent sur d'autres secondes dès que la séquence bouge, donc un
changement de tempo force la remise en place de tous les clips. Oublier la
dernière forme projetée de chaque piste suffit ; la boucle de réconciliation
refait le reste.

## 4. Le panoramique : une position, et une loi qui n'est pas le défaut

`Track::pan` dans `[-1, +1]`, `track.set_pan`, fusion par piste. Deux commandes
et non une à deux champs : deux contrôles différents les bougent, et annuler un
glissé de volume ne doit pas ramener la piste à travers le champ.

La commande porte **une position, jamais deux gains**. Transformer `-0,5` en un
gain gauche et un gain droit est une décision de la projection : un payload qui
porterait des gains figerait la loi dans tous les journaux déjà écrits.

**La loi retenue est `PanLaw3dBCenter`**, l'une des cinq de Tracktion, écrite
explicitement sur chaque plugin de volume à chaque projection. Ce n'est pas
celle que `getDefaultPanLaw()` rend, et c'est délibéré deux fois :

- `getDefaultPanLaw()` est une **globale mutable du processus**. Un projet dont
  l'image stéréo en dépendrait ne rendrait pas pareil sur deux machines, pour la
  même raison qu'une chaîne liée par index ne rechargerait pas pareil.
- ce défaut d'usine est `PanLawLinear`, qui calcule `R = g + pan·g` : à fond à
  droite, `R` vaut `2g`, soit une piste rendue **6 dB plus forte parce qu'elle
  est panoramiquée**.

La consigne disait « reprends la sienne plutôt que d'en inventer une ». Les cinq
sont les siennes ; j'ai signalé laquelle est le défaut et pourquoi elle surprend,
et la décision a été prise avant d'écrire la première ligne.

Preuve mesurée, RMS gauche et droite séparés sur rendu hors ligne :

| Position | Gauche | Droite |
|---|---|---|
| `pan 0` | 0,110272 | 0,110272 |
| `pan -1` | 0,155863 | 0 |
| `pan +1` | 0 | 0,155490 |
| `pan +0,5` | rapport L/R mesuré **0,414214** | la loi dit **0,414214** |

Le rapport attendu est recalculé dans le test depuis la définition de la loi, et
jamais demandé à Tracktion : un test qui demande à Tracktion ce que fait
Tracktion serait d'accord avec n'importe quelle loi. Le centre à 3 dB sous les
extrêmes est vérifié par la même mesure.

**Mono.** Le modèle n'a pas de clip audio, donc toute source est un instrument
MIDI et sort en stéréo. Mais `VolumeAndPanPlugin` n'applique son gain droit que
si le tampon a deux canaux : un tampon mono serait **atténué et non déplacé**.
Le test affirme le nombre de canaux rendus, pour que ce soit un test rouge le
jour des clips audio et pas une découverte à l'oreille.

## 5. Les verbes, et la ligne de coupe tenue

Dix commandes neuves. La liste priorisée a été exposée avant d'écrire, la coupe
arbitrée avant d'écrire, et elle a tenu.

| Commande | Ce qu'elle valide | Fusion |
|---|---|---|
| `tempo.insert` / `remove` / `set_bpm` / `move` | §3 | deux sur quatre |
| `track.set_pan` | §4 | par piste |
| `track.rename` | le nom est ce sur quoi un copilote reçoit un ordre | non |
| `track.reorder` | un index, jamais « monter » | par piste |
| `note.set_velocity` | elle se lisait, elle se change | par note |
| `note.quantize` | départs sur une grille en beats, longueurs intactes | non |
| `note.transpose` | un intervalle, refusé entier s'il sort de `[0, 127]` | non |

**Coupé, comme convenu :** `note.duplicate` et `track.duplicate`. La raison
donnée le lundi n'a pas changé : `track.duplicate` est celui dont la forme du
payload se discute encore — aucune commande n'engendre d'identifiant, donc le
payload doit porter **tous** les identifiants des copies — et un copilote gagne
davantage à quantifier et transposer qu'à dupliquer, ce qu'il sait déjà exprimer
en boucle d'ajouts.

Six commandes solides plutôt que douze bâclées : dix ont tenu, deux sont
reportées, et rien n'est à moitié fait.

## 6. Ce qui rend ces commandes utilisables par le copilote

C'était la demande n° 4, et elle a un test par commande.

`note.quantize` et `note.transpose` prennent une **liste d'identifiants**, jamais
« la sélection ». Une sélection vit dans l'interface : elle n'est ni journalisée
ni annulable, et deux fenêtres sur le même projet en auraient deux. Une commande
dont le sens en dépendrait ne se rejouerait pas, ne passerait pas en JSON-RPC, et
ne serait pas émissible par un agent qui n'a pas de sélection. Le piano-roll lit
la sienne et met les identifiants dans le payload ; tout autre appelant fera
exactement pareil.

Les deux sont **tout ou rien**. Chaque note est lue et vérifiée avant qu'une
seule bouge : une quantification qui en déplacerait la moitié avant de refuser
l'autre laisserait un undoRecord décrivant un état qui n'a jamais existé.

`note.transpose` refuse plutôt que de ramener dans les bornes : en ramenant,
descendre de douze demi-tons puis remonter rendrait un autre accord, et l'undo
cesserait d'être l'inverse de la commande. Son undoRecord porte l'intervalle et
non les hauteurs absolues, pour la même raison — et `note.quantize` relit la
hauteur dans le clip au lieu de la réécrire, pour ne pas défaire une
transposition arrivée entre-temps.

`track.reorder` prend un index et non « monter » : un verbe relatif dépend de
l'endroit où la piste se trouve, et un payload dont le sens dépend de l'état
contre lequel on le rejoue se rejoue autrement.

## 7. L'interface : quatre dettes de la S6 réglées

| Dette S6 | État |
|---|---|
| Pas de renommage de piste | le nom est un libellé éditable au double-clic |
| `track.remove` existe, aucun contrôle ne l'appelle | troisième pastille de la ligne |
| Le piano-roll montre un clip par piste | sélecteur de clip dans l'en-tête, et un bouton qui en crée un |
| Vélocité non éditable | Alt et glisser verticalement sur la note |

Deux choix à défendre :

**Le piano-roll reste à un clip à la fois, délibérément.** Son axe horizontal est
l'étendue d'un clip et non la timeline ; en montrer plusieurs serait une vue
d'arrangement, c'est-à-dire un autre panneau, et le périmètre l'exclut. Le vrai
défaut n'était pas que les autres clips soient hors de l'écran mais qu'ils
étaient **inatteignables** : rien dans l'interface ne permettait ni de les
choisir ni d'en créer un deuxième. Le sélecteur les nomme tous, « + Clip » en
crée un après le dernier.

**La vélocité se change là où elle se lit.** Elle se lit dans la teinte de la
note ; une bande sous le piano-roll aurait coûté un tiers du panneau pour dire la
même chose deux fois. Le glissé est relatif au point de départ, pour qu'une note
à 20 et une note à 120 suivent la main au lieu de sauter dessus.

Le glissé de réorganisation émet sa commande dès que la ligne franchit une
place, pas à la fin : la liste se relit depuis le projet, donc la ligne tenue
suit le curseur. Tout le glissé est un geste, donc une entrée, et l'undoRecord
garde la place de départ.

## 8. Ce que regarder le code a trouvé, et qu'aucun test ne disait

La S6 avait trouvé ses défauts en pilotant le binaire. Cette semaine ils sont
sortis en écrivant, et trois d'entre eux étaient des bombes à retardement.

| Défaut | Ce qui l'a montré | Correction |
|---|---|---|
| `scripts/check-format.sh` ne regardait que les fichiers **suivis** par git | un fichier neuf passait vert en local et rouge en CI — le premier push de la semaine | `git ls-files --cached --others --exclude-standard` |
| La liste de pistes reconstruisait ses lignes dès que l'ordre changeait | aurait détruit la ligne en cours de glissé **pendant sa propre gestion d'événement** | mêmes pistes dans un autre ordre = lignes déplacées, pas rebâties ; sauve aussi un nom en cours de frappe |
| La pastille de suppression appelait le bus depuis son propre callback de clic | la reconstruction détruisait la ligne avant que le callback ne rende la main | la commande part par `callAsync` |
| Le nom éditable interceptait le clic de sélection | cliquer le nom d'une piste ne la sélectionnait plus | le libellé renvoie tout sauf le double-clic à la ligne |
| Un clic dans l'en-tête du piano-roll posait une note | hauteur ramenée dans les bornes, donc note en 127. **Antérieur à cette semaine** | les clics hors de la grille ne font rien |

Le premier a fait échouer un push et mérite d'être dit : un outil d'hygiène qui
ne regarde pas les fichiers neufs est un outil qui valide surtout ce qui ne
change pas.

## 9. Périmètre

Respecté. Aucune ligne d'IA, aucun service Python, aucun moteur harmonique,
aucune branche d'arrangement, aucune automation, aucun ramasse-miettes, et les
trois autres workspaces sont restés des panneaux de remplacement nommés.

Une chose a été faite en plus du périmètre et elle est signalée : la correction
de `check-format.sh`, parce qu'elle conditionne « CI verte à chaque push ».

## 10. Dette

| Dette | État |
|---|---|
| Tempo non modifiable par commande | **Payée.** Quatre commandes, séquence, migration |
| Pas de renommage ni de suppression de piste dans l'interface | **Payée** |
| Le piano-roll montre un clip par piste | **Payée autrement**, voir §7 : les clips sont atteignables, pas simultanés |
| Vélocité non éditable | **Payée** |
| Un jeton absent rend 0 en silence | Inchangée depuis la S6. Trois lignes de `jassert` en debug |
| Pas de vu-mètres | Inchangée. Demande une source de niveau |
| Écriture synchrone à chaque commande | Inchangée depuis la S5, toujours mesurée acceptable |
| Duplication de piste et de note | **Nouvelle, par décision.** Coupée de la S7, voir §5 |
| Pas de vue d'arrangement | **Nouvelle, par constat.** Le piano-roll ne remplacera pas une timeline |
| Le portage Linux | Hors périmètre MVP |

## 11. Ce qui n'a pas été vérifié

**Aucun des contrôles de cette semaine n'a été regardé à l'œil.** Les neuf
défauts du §3 de la S6 n'existaient que pour l'œil, et cette semaine
l'outillage de contrôle du poste n'a pas su ouvrir le binaire. Le pan, le
renommage, la suppression, le sélecteur de clip, la vélocité et le glissé de
réorganisation sont compilés, testés côté domaine et côté moteur, et **jamais
vus**.

C'est le seul écart réel de la semaine avec la méthode de la S6, et il est
signalé plutôt que contourné.

## 12. Suivi d'avancement

### 12.1 Les sept semaines écoulées

| S | Visait | État aujourd'hui |
|---|---|---|
| S1 | Socle : outillage, structure, CI | Acquis |
| S2 | Command Bus | Acquis |
| S3 | Le bus pilote un vrai moteur audio | Acquis |
| S4 | Hébergement VST3 et CLAP | Acquis |
| S5 | Persistance et provenance | Acquis |
| S6 | Une interface qui se montre | Acquis |
| S7 | Le vocabulaire du domaine | — |

Aucune décision d'architecture n'a été rouverte depuis la S1.

### 12.2 Ce que la S8 hérite

Le copilote arrive avec dix verbes de plus que la semaine dernière, tous
appelables depuis un payload seul, tous annulables, tous dans le registry, tous
capables de porter `origin = copilot`.

| Ce qu'il saura demander | Depuis |
|---|---|
| créer, nommer, ranger, supprimer une piste | S2, S7 |
| régler volume, panoramique, mute, bypass | S3, S4, S7 |
| poser, effacer, déplacer, allonger une note | S2, S6 |
| **quantifier, transposer, nuancer une sélection** | **S7** |
| **changer le tempo, et le faire changer en route** | **S7** |
| insérer un plugin, régler un paramètre, capturer son état | S4 |

Ce qui manque toujours pour qu'il parle : les services Python JSON-RPC, qui
rouvrent la règle de thread du bus — `rebindToCurrentThread()` existe pour la
passation délibérée, et c'est le premier sujet de la S8.

### 12.3 Les risques

| Risque | Coût s'il se réalise | Ce qui le tient aujourd'hui |
|---|---|---|
| ~~Le tempo scalaire rencontre un clip audio~~ | — | **Éteint cette semaine** |
| Les contrôles de la S7 n'ont pas été regardés | Moyen : un contrôle illisible se découvre en démonstration | Rien. Voir §11 |
| La règle de thread du bus face à une socket Python | Moyen | `rebindToCurrentThread()` |
| Un clip audio arrive sur une piste mono | Faible, et **instrumenté** : le test de pan affirme le nombre de canaux | §4 |
| Un jeton renommé passe inaperçu | Faible mais silencieux | Rien, voir §10 |

### 12.4 Ce qui a tenu cette semaine

1. **Exposer avant d'écrire.** Le modèle de tempo, la loi de pan et la liste
   priorisée ont été exposés et arbitrés avant la première ligne. La loi de pan
   a changé à cet arbitrage, pas après.
2. **Dire quand la consigne et le code ne disent pas la même chose.** « Reprends
   la loi de Tracktion » et « la loi de Tracktion rend une piste 6 dB plus forte
   à fond à droite » ont été mis côte à côte avant de choisir.
3. **Mesurer l'effet, jamais le champ.** Des secondes pour le tempo, deux RMS
   pour le pan. La leçon de la S3 tient pour la cinquième semaine.
4. **Dire ce qui n'a pas été fait.** La duplication est coupée et le dit ; les
   contrôles n'ont pas été regardés et le disent.
