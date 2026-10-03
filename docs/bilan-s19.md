# Bilan de fin de S19 — DAW IA

**Période :** semaine 19 sur 26. Rédigé le 3 octobre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`, push direct. 18 commits S19, de `731b43b` à `69988d0`, plus ce bilan.
**Volume S19 (hors ce bilan) :** 43 fichiers, +3 892 lignes, −260.
**Tests :** 400 cas ctest (+10 depuis la S18), 45 cas Python (inchangé).
**CI :** verte sur `8e62865`, le dernier commit de code (Linux Clang et Windows) ; les deux suivants ne touchent que la vérification.
**Vérifications de l'application (Release) :** `--verify-canvas`, 113 sur 113 ; `--verify-canvas-charge`, 5 sur 9 (les 4 rouges sont la cible de 8 ms, §5) ;
`--verify-fluidite`, 30 sur 31 (le rouge : la playlist entière à 10,8 ms au 95e centile) ; `scripts/verify-quit.ps1`, fermeture en 233 ms, en 2 070 ms avec un démontage bloqué. `--verify` complet : 611 passées, 43 en échec, aucune étape neuve en échec face à `fe78819` (§6).
**Registry :** 59 types, inchangé. **Schéma du projet :** 5, inchangé. **Enveloppe :** v3, inchangée.
**Aucune commande de domaine ajoutée.** Une méthode ajoutée à l'interface `Command` (§8).

## En bref

- **Chantier 0, expédié.** La tête de lecture avance à chaque image ; le processus finit dans les 2 s une fois le
  projet sauvé ; l'écran ne lit plus le nom d'une commande pour savoir ce qu'elle touche ; le copilote retire la
  ligne vide comme l'écran ; la toile et le glissé d'un point d'automation sont mesurés en Direct2D.
- **La toile sait faire ce que faisait le piano-roll :** choisir des notes d'un bloc à l'autre, copier, coller,
  dupliquer, quantifier, transposer, régler les vélocités, éditer le pattern en cours seul (PAT), générer dans
  une bande, ajouter une piste à un pattern. Rien n'est retiré tant que tu n'as pas essayé.
- **Deux causes non trouvées, dites :** pourquoi le processus restait en vie (§4), et pourquoi le glissé d'un point
  d'automation salit plus que sa bande (§5).
- **Tombé :** la cible de 8 ms au 95e centile sur la toile chargée ; le chantier 2, exposé et non commencé (§7).

## 1. Chantier 0

| Point | Ce qui a été fait | Preuve |
|---|---|---|
| 1. `--verify-fluidite`, la tête de lecture | Deux causes. La mesure partait sur `isPlaying()`, ~200 ms avant le premier bloc du moteur, et comptait 9 images figées : elle part au premier mouvement du moteur. Et un vrai à-coup : la règle « jamais en arrière » figeait la tête une image toutes les ~0,4 s. `DrawnPlayhead` avance la tête du temps écoulé à chaque image et la rattrape vers le moteur en 100 ms, sans dépasser ce que le moteur a joué plus un bloc. Seuil inchangé. | Trois tests, gigue de 0 à 20 ms, cassés une fois (§10). `--verify-fluidite` : 139 images, 139 déplacements de la tête. |
| 2. La fermeture | Le processus finit en moins de 3 s. Cause non trouvée (§4) : une garde termine le processus 2 s après la sauvegarde si le démontage bloque, et écrit dans `daw.log` l'étape où il bloquait. | `scripts/verify-quit.ps1` : une fermeture normale, puis une avec `--quit-stall` (un démontage bloqué exprès) ; garde retirée, le second passage tombe au rouge. |
| 3. La règle `note.*` | Option A, validée : une commande déclare ce qu'elle touche (`Command::reach()`, « tout » par défaut), les 7 commandes de notes déclarent « notes ». Le `Receipt` le porte ; `ProjectObserver` le lit à la place du préfixe du nom. Un oubli coûte un repeint en trop, jamais un écran en retard. | `ReachTests` : chaque commande qui déclare « notes » ne change que des notes, état avant et après comparé. Cassé une fois. |
| 4. Le copilote et la ligne vide | Après `audio.remove` ou `track.remove`, le copilote ajoute les mêmes `lane.remove` que l'écran. `audio.remove` ne change pas. | Test : le projet laissé par le copilote est identique, à l'octet, à celui laissé par Suppr. Cassé une fois. |
| 5. La toile en Direct2D | `--verify-canvas-charge` mesure en Direct2D et en logiciel, au 95e centile, sur 256 blocs, en SONG. | §5. |
| 6. Un point d'automation glissé | Une ligne de 8 points sur le master dans le projet de `--verify-fluidite`, et le glissé d'un point mesuré. | §5. |

## 2. Le modèle validé

Exposé, puis validé (« A pour le point 3, modèle validé, go »).

**Sélection.**
- Ctrl + glisser sur un endroit vide prend des notes à l'échelle des notes, et des blocs au-dessus du seuil.
  L'échelle est lue au clic : franchir le seuil pendant le glissé ne change pas ce qui est pris.
- Notes choisies et blocs choisis s'excluent.
- Deux blocs du même pattern : une note est la même note, prise une fois, allumée dans tous les blocs.
- Deux patterns : les notes des deux sont prises ; chaque geste fait une commande par rangée, regroupées en une
  seule entrée d'historique.

**Copier, coller, dupliquer.** Ctrl+C copie les valeurs des notes, écarts gardés en temps de chanson. Ctrl+V colle
dans le bloc sous la main, à la double-croche sous la main ; chaque rangée va à sa piste, une piste absente du
pattern est ouverte. Coller dans un bloc, c'est écrire dans son pattern : tous ses blocs changent. Les notes qui
dépassent la fin du pattern sont laissées, et l'historique le dit (« N hors du pattern »). Sans bloc sous la main,
rien n'est collé. Ctrl+B fait comme dans le piano-roll : la copie juste après, le pattern s'allonge d'une mesure.

**Quantifier, transposer.** Ctrl+Q à la double-croche ; ↑/↓ d'un demi-ton, Ctrl+↑/↓ d'une octave, sur les notes
choisies. Une transposition qui sortirait du clavier est refusée en entier. Ce sont des gestes neufs : le
piano-roll ne quantifiait ni ne transposait, seul le copilote le faisait.

**Vélocités.**
- Une bande de 40 px (`metric.canvas.velocityStrip`) sous la bande de la piste choisie dans le rack, seulement,
  et seulement à l'échelle des notes : 40 px de hauteur au plus, une seule bande ouverte à la fois.
- Un clic règle une tige ; un trait dessine une rampe ; un geste, une entrée.
- Alt + molette sur une note : 4 par cran (`velocityWheelStep`), une entrée par geste, fermée quand la main part.
- La couleur d'une note suit sa vélocité, comme en S18.

**Mode pattern.** Il suit le transport (PAT/SONG), sans bascule propre à la toile : une seule source de vérité,
comme en S7bis. En PAT, la toile montre le pattern en cours seul, comme un bloc à l'origine, cadré à l'échelle des
notes, avec **une bande par canal du rack** ; une bande vide ouvre une octave depuis la hauteur du canal. Chaque
mode retrouve sa vue en revenant.

**Génération dans une bande.** Maj + glisser dans une bande choisit une zone (un pattern, une piste, une plage du
pattern) ; sinon la zone est celle des notes choisies. Ctrl+G ouvre la fenêtre du S16–S17, sous la toile. Les
notes proposées sont grises dans tous les blocs du pattern, la bande s'agrandit pour les montrer. Alt + molette
pour les variantes, Tab pour écrire (une entrée, la rangée ouverte s'il le faut), Échap pour abandonner,
Ctrl+Espace pour écouter. Une zone qui a des notes les retravaille ; une zone vide reçoit une proposition neuve.

**Piste ajoutée.** En PAT, cliquer dans une bande sans rangée ouvre la rangée et écrit la note, en une entrée. En
SONG aussi désormais (§8). Et en SONG, à l'échelle des notes, une bande « + piste » de 16 px sous une ligne quand
un canal du rack n'y joue nulle part : un clic dans un bloc propose ces canaux, le choisi reçoit une rangée dans
le pattern du bloc.

## 3. Ce que la toile remplace, et ce qui a été retiré

| | État à la fin de la S19 |
|---|---|
| La toile (F4) | Tout ce que la ligne du dessous faisait, plus le mode pattern et la piste ajoutée. |
| Le piano-roll (F7) | **Inchangé, pas retiré.** Il ne fait plus rien que la toile ne fasse. |
| La playlist (F5) | Inchangée ; même classe que la toile, sans le drapeau. |

**Rien n'a été retiré.** C'était la règle exposée : le piano-roll part après ton essai. Ce qui partira alors : F7
du manifeste du beatmaker, `PianoRoll*.cpp`, et les étapes de `--verify` et `--verify-fluidite` qui passent par le
piano-roll, réécrites sur la toile — ce n'est pas un petit travail. Le drapeau toile/playlist reste jusqu'à ce que
la toile prenne la place de F5.

## 4. La fermeture : la cause n'est pas trouvée

Non reproduite en 13 fermetures (Release, Debug, en lecture, fenêtre réduite). **Ce qui est en place n'est pas un
correctif, c'est une garde :** `QuitWatchdog` nomme chaque étape du démontage ; une fois le projet, l'apprentissage
et la mise en page sauvés, un fil détaché attend 2 s, puis écrit « quit: stuck in "X" » dans `daw.log` et termine
le processus. Rien de ce que tu as fait n'est perdu : tout est écrit avant que la garde soit armée. Chaque fermeture
note aussi la durée de son démontage.

**La piste la plus sérieuse :** JUCE arrête le fil de synchronisation de l'écran (`VBlankThread`) avec
`stopThread(-1)`, une attente sans limite, si l'écran ne donne plus de signal. Un pilote audio qui ne rend pas la
main est l'autre hypothèse. Si ça se reproduit, `daw.log` dira l'étape.

## 5. Les mesures

Release MSVC, sur un i5-8365U portable. Chrome tournait pendant toutes les mesures, à ~70 % d'un cœur : les chiffres du 95e centile en portent la trace, et ceux du début de semaine (même code de peinture) étaient plus bas.

**La toile chargée** (`--verify-canvas-charge`, 32 lignes, 256 blocs, en SONG), repeints dans une image en mémoire :

| Échelle | Direct2D, médiane | Direct2D, 95e centile | Logiciel, 95e centile | Direct2D 95e, début de semaine |
|---|---|---|---|---|
| Vue d'ensemble | 9,88 ms | 13,09 ms | 10,65 ms | 9,43 ms |
| Approche (24 px par temps) | 6,53 ms | 9,75 ms | 7,81 ms | 9,31 ms |
| Notes (48 px par temps) | 6,69 ms | 9,15 ms | 7,21 ms | 9,09 ms |
| Une image de déplacement | 6,37 ms | 9,21 ms | 7,42 ms | 7,81 ms |

Pour comparer, sur le même morceau : la playlist de la S11, vue d'ensemble, 18,19 ms de médiane et 20,19 ms au
95e centile en Direct2D.

**La cible de 8 ms au 95e centile n'est pas tenue** en Direct2D : la médiane l'est partout, le 95e centile non.

**Le glissé d'un point d'automation** (`--verify-fluidite`) : 16,5 ms par image en médiane, dont 13,8 ms de repeint (14,4 et 11,9 ms en début de semaine), contre 8,0 ms pour repeindre la playlist entière en Direct2D. Plus que repeindre la fenêtre entière :
la zone salie dépasse la bande d'automation. **Cause non trouvée.** Le message du commit `8de2726` l'attribue au
piano-roll et au mixer ouverts devant : **c'est faux**, sans eux le coût reste le même (11,4 ms).

## 6. Le `--verify` complet

Comparé à `fe78819` (la fin de la S18), avec la même disposition de fenêtres (une copie de
`%APPDATA%\DAW IA\DAW IA.layout` remise avant chaque passage) : 611 vérifications passées, 43 en échec, en 22 étapes. **Aucune n'est neuve** : les 22 échouaient déjà sur
`fe78819` : le copilote sans clé et ce qui en dépend, l'automation du master, deux étapes de génération et
une de vélocité du piano-roll, trois étapes S18 de la toile hors de l'écran sur ce long projet. Quatre passages cette fin de semaine :
- les deux premiers ont trouvé l'étape S19 des blocs choisis hors de l'écran, puis deux blocs pris sur trois :
  corrigé dans la vérification (`903a61b`, `69988d0`) ;
- le troisième a vu quatre étapes S16–S17 lire la ligne de la génération un geste en retard (« Entrée sur les
  mêmes mots », l'apprentissage exclu, « 2 projets », « oublier »). Vertes aux trois autres passages, sur un code
  que la S19 ne touche pas. **Intermittent, non diagnostiqué** : la lecture du prompt passe par un rappel, et
  l'étape lit l'écran dans le même appel.

## 7. Ce qui est tombé, ou reste en deçà

- **La cible de 8 ms** sur la toile chargée, au 95e centile en Direct2D (§5).
- **Les deux causes** : la fermeture (§4) et le glissé d'un point d'automation (§5).
- **Le piano-roll n'est pas retiré** : volontaire, il attend ton essai (§3).
- **Le chantier 2 n'est pas commencé.** Il est exposé dans le message de fin de semaine, et attend ton accord :
  l'ancrage des fenêtres, le défilement du channel rack et de l'historique avec la grammaire S18, puis le repeint
  du mixer.

## 8. Les écarts à l'acquis, dits

- **Une méthode ajoutée à l'interface `Command` du domaine** (`reach()`, et le champ `Receipt::reach`). Ce n'est
  pas un verbe ; c'est la décision A du point 3. Une commande qui ne la déclare pas vaut « tout ».
- **En SONG, écrire dans une bande qu'un pattern n'a pas ouvre la rangée.** En S18, la toile ne le faisait qu'en
  mode pattern. Le coller fait de même.
- **La toile suit le mode du transport.** En PAT, elle montre le pattern seul, plus le morceau ; c'était exposé.
- **Un commit qui mêle deux sujets :** `5d7551c` porte quantifier/transposer et les vélocités, écrits dans les mêmes
  fonctions.
- **Un message de commit faux :** `8de2726` (§5).
- **`--verify-canvas-charge` passe en SONG** avant de mesurer (`e386da9`) : la toile s'ouvre désormais sur le mode
  du transport, et en PAT elle ne mesurait qu'un pattern.
- **Mes vérifications réécrivent `%APPDATA%\DAW IA\DAW IA.layout`.** Ta disposition a été copiée avant chaque
  `--verify` complet et remise après.

## 9. Ce qui s'est mal passé, dit

- **Un bug de la S18 trouvé en route :** l'empreinte d'une rangée dans le cache des bandes ne comptait pas la
  vélocité ; Alt + molette partait d'une valeur périmée. Corrigé (`042f89a`).
- **Le rendu du mode pattern était faux alors que l'état était juste :** un bloc jaune peint à la place du pattern,
  parce que le chemin de peinture relisait le placement dans le projet au lieu du bloc virtuel. Trouvé par une
  vérification au pixel ajoutée pour ça.
- **Des étapes S19 hors de l'écran dans le grand projet de `--verify`**, vertes dans le petit de
  `--verify-canvas` : la molette qui ramène une rangée en vue est allée jusqu'à 400 crans, et l'étape des blocs
  choisis cadre le bloc au lieu de montrer « tout le morceau », qui garde le zoom.
- **Mes mesures ont tourné avec ton Chrome chargé** (~80 % d'un cœur) : les chiffres en portent la trace (§5).

## 10. La règle du test cassé une fois

Chaque test neuf passé du premier coup a été cassé une fois, et tous sont passés au rouge. Ceux qui ne l'ont
pas fait du premier coup, et ce qui a changé :
- **`DrawnPlayhead` :** une gigue trop douce laissait passer l'ancienne règle ; portée à 20 ms.
- **La sélection qui ne compte une note qu'une fois :** la bande ne traversait qu'un bloc du pattern ; élargie.
- **Les vélocités :** casser le calcul de la rampe ne suffisait pas ; c'est l'écriture qui a été cassée.
- **Le retour en SONG :** la vue de départ était à l'origine, qu'un retour raté rendait aussi ; elle part loin.
- **Le mode pattern :** l'état passait, le rendu non ; d'où le pixel.
Les autres : `Reach`, le copilote et la ligne vide, `copyFromBlocks`, `CanvasBands::ofPattern`, la garde de
fermeture, la bande « + piste » (non peinte, et la rangée refusée en SONG : deux rouges).

## 11. À essayer, dans l'ordre

Sur `main`, Release et Debug recompilés dans ton dossier de build. Ferme Chrome si tu peux.

1. **La tête de lecture.** Lance la lecture en SONG : elle doit glisser sans à-coup.
2. **Fermer.** Ferme l'application : le processus doit disparaître en moins de 3 s (gestionnaire des tâches).
3. **Sélection.** F4, Ctrl + molette jusqu'aux notes. Ctrl + glisser d'un bloc à un autre : les notes prises
   s'allument aussi dans l'autre bloc du même pattern. Dézoome et refais : ce sont les blocs qui sont pris.
4. **Copier, coller, dupliquer.** Ctrl+C, main sur un autre bloc, Ctrl+V. Puis Ctrl+B.
5. **Transposer, quantifier.** ↑/↓, Ctrl+↑/↓, Ctrl+Q.
6. **Vélocités.** Clique un canal dans le rack, dessine un trait dans la bande de 40 px. Alt + molette sur une
   note. Dis-moi si 40 px suffisent à la main.
7. **PAT.** Clique PAT : la toile montre le pattern seul, une bande par canal. « + Pattern », puis clique dans une
   bande : la rangée s'ouvre. Reviens en SONG : ta vue est là.
8. **Générer dans une bande.** En PAT, Maj + glisser dans une bande vide, Ctrl+G, « une mélodie en la mineur »,
   Entrée. Alt + molette, puis Tab. Avec la clé d'API ou sans, en local.
9. **« + piste ».** En SONG, à l'échelle des notes, ajoute un canal au rack : une bande « + piste » apparaît sous la
   ligne. Clique-la dans un bloc.
10. **Et ensuite :** dis-moi si le piano-roll peut partir.

## 12. Reste à faire

- Ton essai (§11), et ton accord pour retirer le piano-roll.
- Le chantier 2, une fois exposé et validé.
- Les deux causes non trouvées (§4, §5), et la cible de 8 ms (§5).
- Le bug intermittent de la lecture : il ne s'est pas présenté cette semaine.
