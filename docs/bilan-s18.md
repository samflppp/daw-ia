# Bilan de fin de S18 — DAW IA

**Période :** semaine 18 sur 26. Rédigé le 1er octobre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`, push direct. 12 commits S18, de `523053e` à `a3e428d`, plus ce bilan.
La S18 bis (la fluidité, `25cce40` à `7c82502`) a été menée en parallèle par une autre session et a son propre
bilan (`docs/bilan-s18bis.md`) ; elle a été fusionnée dans la toile avant que la toile n'arrive sur `main`.
**Volume S18 (hors S18 bis et ce bilan) :** 48 fichiers, +3 657 lignes, −246.
**Tests :** 390 cas ctest (domaine, persistance, interface ; +23 depuis la S17), 45 cas Python (+2). Moteur
inchangé cette semaine : ses 89 cas n'ont pas été relancés ici.
**CI :** verte sur `a3e428d`, Linux (Clang) et Windows.
**Vérifications de l'application :** `--verify-canvas`, 23 sur 23 ; `--verify-canvas-charge`, 4 sur 4 (Release).
**Registry :** 59 types, inchangé. **Schéma du projet :** 5, inchangé. **Enveloppe :** v3, inchangée.
**Aucune commande de domaine ajoutée.**

## En bref

- **Chantier 0, expédié.** Le fader d'une tranche automatisée suit la courbe en lecture ; la ligne d'une piste
  part avec son dernier clip audio ; le lecteur local comprend « fa dièse mineur » ; la sensible du mineur
  harmonique est acceptée par les règles de justesse.
- **La toile (F4).** La playlist qui zoome jusqu'aux notes : chaque ligne se déplie en une bande par piste, la
  grille apparaît en fondu, et au seuil les notes s'attrapent là où elles sont dessinées. Les autres blocs du même
  pattern s'allument sous la main. Tu l'as essayée : « tt est good ».
- **La grammaire (chantier 2).** Ctrl + molette zoome partout, le clic-molette déplace la vue partout, un fader ne
  prend la molette qu'après un clic, F cadre et Maj+F montre l'ensemble, et le beatmaker s'ouvre sans fenêtres qui
  se recouvrent.
- **Repeint de la toile sur un projet chargé, Release :** 9,5 ms en vue d'ensemble, 4,4 ms à l'échelle des notes,
  4,1 ms par image de déplacement.
- **Tombé :** le mode pattern dans la toile, l'ancrage vrai des fenêtres (§7).

## 1. Chantier 0

| Point | Ce qui a été fait | Preuve |
|---|---|---|
| Fader automatisé | `automationEditing::shownValue` : en lecture de la chanson, la valeur que joue la ligne ; à l'arrêt et en mode pattern (où aucune automation ne joue), la valeur du projet. Pendant un geste, la main ; relâché, la courbe. Même règle pour le pan, et pour les curseurs de la liste des pistes. Le bouger écrit toujours la valeur du projet ; le `ParameterBridge` ne change pas. | Test, cassé une fois. Non vu à l'oreille ici : pas de lecture sans carte son. |
| Ligne vide d'une piste audio | Retirer le dernier clip audio d'une piste, ou la piste, retire sa ligne si elle reste vide et sans nom, dans la même entrée d'historique (`laneEditing`). | Tests : un Ctrl+Z rend le projet à l'octet ; une ligne nommée, ou qui porte un bloc de pattern, reste. Cassé une fois. |
| « fa dièse mineur » | Le lecteur local lit la tonalité en français : « dièse », « bémol », « fa#m », « sol dièse ». Un nom de note seul n'est pas une tonalité : « la mélodie », « si douce » ne demandent rien. | Test, cassé une fois. L'entrée est retirée d'`IDEES.md`. |
| Mineur harmonique | `degreeIndex` / `isLegal` en C++, `degree_index` en Python, épinglés par les mêmes cas : le sol# en la mineur compte comme septième degré dans l'apprentissage local et dans le pipeline du corpus. `diatonicIndex` et `legalPitches` ne changent pas : le générateur écrit la gamme naturelle, et les tests S14–S15 n'ont pas bougé. Le mineur mélodique n'est pas dans la règle. | Tests des deux côtés, cassés une fois des deux côtés. |

**À savoir sur le dernier point :** le code du générateur ne change pas, mais un style appris sur des sol# compte
désormais ces notes. Ce que le générateur tire d'un style appris peut donc changer, pour cette entrée-là.

## 2. Le modèle des échelles retenu

Exposé, puis validé (« A, go »).

| Question | Réponse | Pourquoi |
|---|---|---|
| Combien d'échelles | Un zoom continu ; ce qui change, c'est ce qu'on peut attraper. Trois états lisibles : les blocs, l'approche (la grille apparaît en fondu, les notes prennent leur couleur), les notes. | Le passage se voit avant d'être franchi : la grille et la couleur annoncent le seuil. |
| Le seuil | Une double-croche de 8 px et une rangée de 7 px. La hauteur d'une rangée suit la largeur d'une double-croche (7/8), si bien que les deux axes franchissent le seuil ensemble. 14 px au plus, la touche du piano-roll ; 192 px par temps au plus. | Une seule molette, deux axes : pas de second zoom à apprendre. |
| Sous le pixel | Une rangée de moins d'un pixel n'est pas dessinée : le bloc montre l'aperçu de la S11. | « Rien plutôt qu'une bouillie ». |
| Le zoom autour du pointeur | Le temps sous le pointeur reste sous le pointeur, et sur la toile la rangée aussi, à chaque image du zoom qui glisse (S18 bis). | La main ne perd pas ce qu'elle visait. |
| Où vit l'état de vue | Dans l'écran : zoom, position, rangées ajoutées à une bande, notes choisies. Jamais dans le projet ni dans l'historique. | Même raison que la sélection en S9 §6.4. |

## 3. La question des pistes dans un bloc

**Réponse retenue : une bande par piste** (l'option A). Une ligne se déplie en autant de bandes que de pistes jouées
par les patterns qui y sont posés, dans l'ordre du channel rack. Chaque bande est un petit piano-roll cadré sur ce
que sa piste y joue : ses notes extrêmes et deux demi-tons de marge, une seule rangée pour un sampler, une octave
depuis la hauteur du canal tant qu'il n'y a pas de note. On clique dans la bande du Lead, la note va au Lead :
aucun choix de canal.

**Ce que ça a coûté :** des lignes de hauteur variable, donc la géométrie de la playlist passée par `laneTop` /
`laneHeightOf` (une ligne de playlist garde sa hauteur fixe) ; un cache par (pattern, piste) au lieu de par
pattern ; et le bord d'une bande qu'on tire pour ajouter des rangées, un état d'écran.

**Une bande qui respire, et qui se tient sous la main :** la plage d'une bande suit les notes, mais pendant qu'une
note est glissée, les bandes de sa ligne sont figées. Sinon, une note amenée en haut agrandirait la bande, les
rangées glisseraient sous le pointeur et la note monterait toute seule jusqu'à 127.

**Éditer une note, c'est éditer le pattern.** Dès que la main est sur un bloc, à l'échelle des notes, les autres
blocs du même pattern s'allument (fond et contour) et le bloc sous la main dit « ×N ». Le déplacement passe par un
geste du bus : les autres blocs bougent en direct, sous les yeux. Une note glissée fait une entrée d'historique.

## 4. Ce que la toile remplace, et ce qui reste

| | État à la fin de la S18 |
|---|---|
| La toile (F4) | Ouverte par défaut au centre du beatmaker. Mêmes lignes, mêmes blocs, mêmes gestes que la playlist, plus les notes. |
| La playlist (F5) | Inchangée, fermée par défaut ; elle s'ouvre à la place de la toile. C'est la même classe : la toile est la playlist construite avec un drapeau. |
| Le piano-roll (F7) | Inchangé, fermé par défaut. Il reste la seule place pour les vélocités, le copier-coller de notes, la quantification, la transposition et la génération S16. |

**Ce qui disparaîtra :** la page du piano-roll, quand la toile saura faire ce que la ligne du dessus énumère ; puis
le drapeau, quand la toile aura pris la place de F5. Rien n'a été supprimé cette semaine.

## 5. Les mesures de repeint

Release MSVC, `--verify-canvas-charge` : 32 lignes, 256 blocs, 16 patterns de 4 pistes et 128 notes, page de
1084 × 482. Médiane de 20 repeints complets du panneau, dessinés dans une image en mémoire : le rendu logiciel de
JUCE, pas l'écran. C'est une autre méthode que celle de la S18 bis (Direct2D, 95e centile) : les chiffres des deux
bilans ne se comparent pas un pour un.

| Échelle | Premier passage | Avec les blocs en image | La playlist S11, même morceau |
|---|---|---|---|
| Vue d'ensemble | 59,1 ms | **9,5 ms** | 11,1 ms |
| Approche (24 px par temps) | 19,3 ms | **4,4 ms** | — |
| Notes (48 px par temps) | 10,4 ms | **4,4 ms** | — |
| Une image de déplacement | 12,4 ms | **4,1 ms** | — |

**Ce qui a fait la différence :** tous les blocs d'un même pattern sur une même ligne se ressemblent à un zoom
donné. La toile en dessine un dans une image et le pose autant de fois qu'il est posé. L'image part quand le
contenu change, ou la taille ; jamais quand la vue bouge seulement. Vingt images de déplacement ne redessinent ni
une rangée ni une image : c'est vérifié, et la vérification passe au rouge quand on met la position de la vue dans
la clé du cache.

**La cible de 8 ms** est tenue à toutes les échelles sauf la vue d'ensemble (9,5 ms), qui reste sous la playlist
de la S11 sur le même morceau.

## 6. Le chantier 2 : la grammaire

Exposée, validée en bloc (« on valide tout »).

| Geste | Partout |
|---|---|
| Molette | fait défiler l'axe vertical, ou le seul axe du panneau |
| Maj + molette | fait défiler le temps |
| Ctrl + molette | zoome autour du pointeur. **Le piano-roll le gagne** : il y faisait défiler les hauteurs. |
| Molette sur la règle | zoome aussi, par habitude de FL |
| Alt + molette | règle finement ce qui est sous la main : la courbe d'un point, la variante d'une proposition |
| Clic-molette + glisser | déplace la vue. **Gagné par** le mixer, la liste des pistes, la chaîne de plugins et le navigateur. |
| Molette sur un curseur | le change, une fois cliqué, jusqu'à ce que le pointeur le quitte. Avant, elle fait défiler ce qui l'entoure. |
| F / Maj+F | cadre ce qui existe / montre l'ensemble, dans le piano-roll, la playlist et la toile |

**Le cadrage.** Le piano-roll cadre les notes choisies, sinon toutes : leur durée remplit la largeur, leurs
hauteurs sont centrées dans la fenêtre au lieu de flotter parmi 128 touches. La playlist et la toile cadrent les
blocs choisis, la toile sinon le bloc sous la main. Le calcul est pur (`ViewFraming`), testé et cassé une fois.

**Les fenêtres.** Le beatmaker s'ouvre sans recouvrement : le navigateur à gauche, la toile au centre, le channel
rack dessous, l'historique et le copilote à droite. Le mixer, les plugins et les pistes restent des fenêtres qui
passent devant, ouvertes à la demande.

**Hors du tableau :** le channel rack et l'historique ne défilent pas du tout ; un rack de vingt canaux déborde.
C'est noté pour la suite.

## 7. Ce qui est tombé, ou reste en deçà

- **Le mode pattern dans la toile** (le pattern en cours seul, comme un bloc à l'origine). Exposé dans le modèle,
  pas construit.
- **L'ancrage vrai des fenêtres.** Ce qui est livré, c'est une disposition par défaut sans recouvrement ; une
  fenêtre déplacée à la main peut encore en recouvrir une autre, et les pages ne se redimensionnent pas ensemble.
- **Dans la toile :** les vélocités, le copier-coller de notes, la quantification, la transposition, la génération
  dans une bande, ajouter une piste à un pattern. Noté dans `IDEES.md`, avec « rendre un bloc unique », qui serait
  un verbe de domaine.

## 8. Les écarts à l'acquis, dits

- **Le fader montre la courbe en lecture.** Écart voulu à la S13 §2, décidé dans la commande de la semaine.
- **La ligne d'une piste audio part avec son dernier clip** par un groupe que fait l'écran (`audio.remove` +
  `lane.remove`), pas par `audio.remove`. Changer ce que fait la commande changerait ce que rejouent les journaux
  écrits depuis la S17. Conséquence : le copilote, qui appelle `audio.remove` seul, laisse encore la ligne.
- **Le manifeste du beatmaker change de pages.** Les places des fenêtres gardées par l'application pour l'ancienne
  disposition sont oubliées une fois, comme prévu depuis la S11.
- **Dans la toile, Échap ne montre plus la chanson entière** : c'est Maj+F, comme partout.
- **La vérification complète ferme la toile au départ**, pour que les étapes d'avant la S18 retrouvent la playlist
  pour laquelle elles ont été écrites.

## 9. Ce qui s'est mal passé, dit

- **Deux sessions dans le même dossier.** La S18 bis a mis de côté mon travail en cours sur une branche, sans perte.
  La toile a ensuite été construite dans un worktree à part (`DAW IA toile`), puis fusionnée.
- **Mes vérifications ont écrit dans ton projet « Sans titre ».** L'application rouvre par défaut
  `Documents\DAW IA\Sans titre.dawproj`. Six entrées (934 à 1 245 du journal) venaient de mes passages. Après une
  sauvegarde, un essai sur une copie, puis six annulations, ton projet est identique à l'octet à ton état de
  l'entrée 933. Depuis, chaque vérification tourne avec `--project` dans un dossier jetable.
- **Le disque C: s'est rempli** pendant la compilation. Vingt-huit gigaoctets de dossiers temporaires d'anciennes
  sessions sur un autre projet ont été supprimés, sur ton accord.
- **Ton DAW n'a pas quitté à la fermeture.** La fenêtre était partie, le processus tournait encore ; il a été tué
  sur ton accord. **C'est un bug, non diagnostiqué.**

## 10. La règle du test cassé une fois

Chaque test neuf passé du premier coup a été cassé une fois dans le code qu'il regarde, et tous sont passés au
rouge :
- `shownValue` qui ignore la lecture ;
- `linesLeftEmpty` qui ne rend plus rien ;
- la tonalité en français jamais lue ;
- la sensible refusée, en C++ et en Python ;
- les bandes, sept cas, cassés ensemble : marge, cache, fenêtre de notes, échelle, hauteur de ligne, rangées
  ajoutées, ligne vide ;
- `ViewFraming`, trois cas ;
- le cache d'images de blocs, par la vérification au rendu.

**Un rouge qui ne regardait pas le bon objet.** Au premier passage de `--verify-canvas`, une note écrite par la
vérification n'était pas vue par l'étape suivante : la commande avait été jouée dans le même appel que le clic, et
la toile ne l'avait pas encore entendue. Les étapes ont été coupées pour laisser passer les messages, et la toile
rattrape désormais le projet avant de lire la main (`catchUp`).

## 11. À essayer, dans l'ordre

Sur `main`, compilée chez toi.

1. **La disposition.** Ouvre le beatmaker : rien ne se recouvre. La toile au centre, le rack dessous.
2. **La toile, de loin à près.** Ctrl + molette sur un bloc : la grille doit apparaître avant que les notes
   s'attrapent. Dis-moi si c'est continu ou si ça saute.
3. **Écrire dans un bloc.** Sur un pattern posé deux fois, survole un bloc : l'autre s'allume. Clique dans une
   bande : la note apparaît dans les deux.
4. **F et Maj+F**, dans la toile puis dans le piano-roll (F7) : cadrer, revenir.
5. **Le mixer (F10) qui défile.** Passe la molette sur les faders sans cliquer : la rangée doit défiler, aucun
   volume ne doit bouger. Clique un fader, puis molette : il bouge. Clic-molette et glisser : la rangée suit.
6. **Le fader automatisé.** Une ligne de volume, lecture en SONG : le fader suit la courbe. Prends-le pendant la
   lecture, lâche-le : il revient sur la courbe.
7. **En local, sans copilote :** « une boucle trap en fa dièse mineur ».

## 12. Reste à faire

- Ton essai (§11), avec carte son, et ce que tu en penses à l'oreille pour le fader.
- La fermeture qui laisse le processus en vie.
- Le mode pattern dans la toile, puis ce qu'il faut à la toile pour remplacer le piano-roll.
- Le bug intermittent de la lecture : il ne s'est pas présenté cette semaine.
