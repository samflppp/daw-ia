# Bilan S18 et S18 bis — DAW IA

**Période :** semaine 18 sur 26, du 29 septembre au 2 octobre 2026. Rédigé le 2 octobre 2026.
**Ce document :** la synthèse des deux chantiers de la semaine, menés en parallèle par deux sessions. Le détail,
les tableaux de mesures et les preuves restent dans `docs/bilan-s18bis.md` (la fluidité) et `docs/bilan-s18.md`
(la toile, la grammaire, le chantier 0).
**Dépôt :** `samflppp/daw-ia`, branche `main`. S18 bis : `25cce40` à `7c82502`. S18 : `523053e` à `ada24c9`.
**Tests à la fin de la semaine :** 390 cas ctest, 45 cas Python. CI verte sur `a3e428d`, Linux (Clang) et Windows.
**Domaine :** aucune commande ajoutée, registry à 59 types, schéma du projet 5, enveloppe v3 — rien n'a bougé sous
l'interface.

## En une phrase

La S18 bis a rendu l'interface assez rapide pour que la S18 puisse lui demander plus : une playlist qui zoome
jusqu'aux notes et qu'on édite sur place, sous une seule façon de naviguer partout.

## En bref

- **La toile (F4).** La playlist zoome en continu jusqu'au piano-roll. Chaque ligne se déplie en une bande par
  piste ; la grille apparaît en fondu ; au seuil, les notes s'attrapent là où elles sont dessinées. Une note écrite
  dans un bloc l'est dans le pattern : les autres blocs du même pattern s'allument et bougent en direct. Essayée
  le 2 octobre : « ça marche, j'aime beaucoup ».
- **La fluidité.** Le repeint de la playlist passe de 24,95 ms à 4,79 ms (95e centile, cible 8 ms), le
  déplacement d'une fenêtre interne de 65,36 ms à 6,55 ms. La fenêtre principale est déplacée par Windows (ancrage
  et dispositions de Windows 11 compris), la tête de lecture avance à chaque image de l'écran.
- **La grammaire.** Ctrl + molette zoome partout, le clic-molette déplace la vue partout, un fader ne prend la
  molette qu'après un clic, F cadre ce qui existe et Maj+F montre l'ensemble.
- **Le chantier 0.** Le fader automatisé suit la courbe en lecture ; la ligne d'une piste audio part avec son
  dernier clip ; le lecteur local comprend « fa dièse mineur » ; la sensible du mineur harmonique est acceptée.
- **Fluide / Léger** dans Fichier > Affichage, et quatre animations du mode fluide, validées puis codées.

## 1. S18 bis : la fluidité

Le chantier partait d'un diagnostic lu dans le code ; la mesure en a confirmé l'essentiel et en a corrigé deux
points.

**Ce qui coûtait vraiment.**
- `Tokens` relisait `tokens.json` à chaque appel. C'était le plus gros coût du piano-roll (27,26 → 5,18 ms rien
  qu'en l'aplatissant au chargement).
- La playlist était O(n²) par repeint : chaque conversion d'un temps en pixel reconstruisait la liste des blocs et
  reparsait leurs identifiants. Le texte, soupçonné d'abord, coûtait 10 µs par libellé.
- Un glissé de note coûtait 26 ms par image, surtout ailleurs que dans le panneau glissé : le mixer rafraîchissait
  ses 17 tranches à chaque commande, la playlist se repeignait entière.
- La tête de lecture avait deux causes d'à-coups : un minuteur de 33 ms, et un transport Tracktion qui n'avance
  qu'à chaque bloc audio (~43 fois par seconde).

**Ce qui a été fait.** Une table de tokens ; `setOpaque` sur 17 composants ; un cache de la playlist reconstruit
seulement quand le projet change ; des repeints limités à ce qui bouge ; un `FrameTicker` calé sur l'écran, avec
une position de tête de lecture portée en avant de 50 ms au plus ; la barre de titre confiée à Windows ; le mode
Léger (30 images par seconde, moteur logiciel). Les durées d'animation sont devenues des tokens `motion.*`, et la
règle d'hygiène n°1 les voit.

**Les animations du mode fluide,** exposées puis validées : la page qui tourne sous la tête de lecture, le zoom
amorti, les vumètres qui retombent avec une crête qui tient, les fenêtres internes en fondu à l'ouverture.

## 2. S18 : la toile

**Le modèle, exposé puis validé.** Un seul zoom continu ; ce qui change avec lui, c'est ce qu'on peut attraper.
Trois états lisibles : les blocs, l'approche (la grille en fondu), les notes. Le seuil — une double-croche de 8 px,
une rangée de 7 px — est franchi par les deux axes ensemble, parce que la hauteur des rangées suit la largeur des
temps : une seule molette, pas de second zoom à apprendre.

**Une bande par piste** (l'option A, choisie). On clique dans la bande du Lead, la note va au Lead : aucun choix de
canal. Pendant qu'une note est glissée, les bandes de sa ligne sont figées, pour qu'une note amenée en haut ne
fasse pas grandir sa bande et monter toute seule.

**Le repeint, Release, sur 256 blocs.** 9,5 ms en vue d'ensemble, 4,4 ms à l'échelle des notes, 4,1 ms par image
de déplacement. Le premier passage faisait 59 ms : chaque bloc d'un même pattern se ressemble à un zoom donné, il
est maintenant dessiné une fois dans une image et posé autant de fois qu'il est posé. La cible de 8 ms est tenue
partout sauf en vue d'ensemble, qui reste sous la playlist de la S11 sur le même morceau. (Ces chiffres sont pris
au moteur logiciel, ceux de la S18 bis en Direct2D : ils ne se comparent pas un pour un.)

**Ce que la toile ne remplace pas encore.** Le piano-roll (F7) reste la seule place pour les vélocités, le
copier-coller de notes, la quantification, la transposition et la génération. La playlist (F5) est toujours là,
fermée par défaut : c'est la même classe, la toile en est une construction avec un drapeau.

## 3. Les écarts à l'acquis, dits

Repris des deux bilans, les plus lourds de conséquence :
- **Le fader montre la courbe en lecture**, écart voulu à la S13 §2.
- **La ligne d'une piste audio part par un groupe que fait l'écran** (`audio.remove` + `lane.remove`), pas par
  `audio.remove`, pour ne pas changer ce que rejouent les journaux écrits depuis la S17. Le copilote, qui appelle
  `audio.remove` seul, laisse encore la ligne.
- **La playlist lit le type des commandes** (`note.*`) pour savoir que seules des notes ont changé : une convention
  de nommage devenue une règle d'écran.
- **La tête de lecture dessinée peut devancer le moteur de 50 ms**, position d'affichage séparée de celle de la
  logique.
- **Une fenêtre agrandie qu'on tire se restaure et suit la main**, comme sous Windows ; l'ancienne règle est tombée.
- **Le beatmaker change de pages** : la toile au centre, sans recouvrement. Les places mémorisées pour l'ancienne
  disposition sont oubliées une fois.

## 4. Ce qui s'est mal passé, dit

- **Deux sessions dans le même dossier.** La S18 bis a rangé la toile en cours sur une branche ; la toile a été
  finie dans un worktree, puis fusionnée. Rien de perdu ; worktree et branches supprimés depuis.
- **Mes vérifications ont écrit dans ton projet « Sans titre ».** Six entrées défaites, projet rendu identique à
  l'octet. Depuis, chaque vérification tourne avec `--project` dans un dossier jetable.
- **Le disque s'est rempli deux fois** pendant les compilations et les rendus. Trois « project not saved: database
  or disk is full » au journal pendant la S18 bis.
- **Ton DAW n'a pas quitté à la fermeture** une fois. Processus tué sur ton accord ; non diagnostiqué.
- **F4 n'ouvrait pas la toile chez toi** (2 octobre). Deux causes. Ton clavier envoie la luminosité sur F4 sans
  Fn. Et surtout, ton exe Debug datait du 29 septembre : la toile avait été compilée dans le worktree, jamais dans
  ton dossier de build. Recompilé ; ça marche. **L'exe Release date encore du 30 septembre**, d'avant la toile.

## 5. La méthode, tenue

Chaque test ou vérification neuf qui passait du premier coup a été cassé une fois dans le code qu'il regarde, et
tous sont passés au rouge — sept dans la S18 bis, une dizaine dans la S18. Deux vérifications étaient aveugles et
ont été trouvées autrement : un glissé de note mesuré à 0 ms (la main allongeait la note au lieu de la déplacer),
et une étape « rendre la taille » qui passait sans que la fenêtre ait jamais été agrandie.

`--verify` complet sur le code de la S18 bis : 510 passées, 38 en échec, les mêmes que la référence une pour une
(le copilote absent). `--verify-canvas` : 23 sur 23. `--verify-canvas-charge` : 4 sur 4. `--verify-fluidite` :
27 sur 27 avant les animations ; après, la mesure de la tête de lecture au rouge sous charge, à refaire (§6).

## 6. Reste à faire

- **Recompiler le Release** si tu t'en sers : il est d'avant la toile.
- **Tes essais à la main** : la grille de la toile qui apparaît avant le seuil (continu ou à sauts ?), le fader
  automatisé à l'oreille, les quatre animations à l'œil, le 144 Hz si tu as l'écran.
- **`--verify-fluidite`** refait avec aucune autre instance de DAW IA ouverte : la mesure de la tête de lecture est
  tombée au rouge sous la charge de ton instance Debug, le seuil n'a pas été relâché.
- **La fermeture qui laisse le processus en vie.**
- **Ce qu'il manque à la toile pour remplacer le piano-roll**, puis le mode pattern dans la toile ; noté dans
  `IDEES.md`.
- **L'ancrage vrai des fenêtres** : la disposition par défaut ne se recouvre pas, mais une fenêtre déplacée à la
  main peut encore en couvrir une autre. Le channel rack et l'historique ne défilent pas.
