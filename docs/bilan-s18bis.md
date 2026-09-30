# Bilan de S18 bis — DAW IA

**Période :** semaine 18 bis, chantier 0 bis de la S18, avant la toile. Rédigé le 30 septembre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`. 12 commits de `25cce40` à `9836d1c`, plus ce bilan. Pas encore
poussés : voir « Reste à faire ».
**Volume :** 53 fichiers, +2 160 lignes, −290 (hors ce bilan et `IDEES.md`).
**Tests :** 362 cas de domaine (+3, `TokenTableTests`), un cas élargi (`PreviewTests`). Aucune commande de domaine
ajoutée, `ProjectState`, journal et projection intacts.
**Mesures :** Release MSVC, un écran 60 Hz (58 images par seconde mesurées), échelle 1,0.

## En bref

- **La cible est atteinte.** Un repeint complet de la playlist sur le projet de mesure : **24,95 ms → 4,79 ms**
  au 95e centile (cible 8 ms). Une image de déplacement de fenêtre interne : **65,36 → 6,55 ms**.
- **La fenêtre principale est déplacée par Windows**, plus par JUCE : ancrage aux bords et dispositions de
  Windows 11 comprises.
- **La tête de lecture avance à chaque image de l'écran.** Il y avait deux causes d'à-coups, pas une.
- **Fluide / Léger** dans Fichier > Affichage, appliqué sans redémarrer.
- **Tombé :** les animations du mode fluide ne sont pas codées. C'est la méthode, pas le temps : leur liste est
  en §9, à valider.

## 1. Le diagnostic, confirmé ou corrigé par la mesure

| # | Diagnostic lu dans le code | Ce que la mesure dit |
|---|---|---|
| 1 | La barre de titre déplace la fenêtre par `ComponentDragger`, un `setBounds` par mouvement | Confirmé dans le code. Un déplacement natif ne se chronomètre pas par un repeint JUCE : c'est à essayer à la main (§8). |
| 2 | `Tokens` relit `tokens.json` à chaque appel | **Confirmé, et c'était le plus gros coût du piano-roll** : 27,26 → 5,18 ms rien qu'avec B. Le déplacement de fenêtre interne passe de 65 à 29 ms. |
| 3 | Aucun `setOpaque`, JUCE repeint derrière chaque panneau | **Corrigé.** JUCE repeint toujours les ancêtres dans la zone salie, opaque ou non ; `setOpaque` lui permet seulement de sauter les frères couverts. Mesuré quand même : sur le déplacement d'une fenêtre interne, 13,55 ms sans, 6,41 ms avec (le cache étant là). |
| 3 bis | `PlaylistPanel::paint` reconstruit `items()` et reparse des identifiants | **Corrigé : c'était pire.** Chaque conversion d'un temps en pixel (`xForBeat`) passait par `fitBeatWidth`, `timelineBeats`, `items()` et trois parses d'identifiant par bloc : O(n²) par repeint. Le texte, soupçonné d'abord, coûte 10 µs par libellé. |
| 4 | Minuteur de 33 ms, pas calé sur l'écran | **Confirmé, et une seconde cause trouvée :** le transport de Tracktion n'avance qu'à chaque bloc audio, ~43 fois par seconde ici. Calé sur l'écran seul, une image sur quatre voyait encore la même position (106 déplacements pour 139 images). |
| 5 | Un glissé repeint tout le panneau | **Confirmé, et ce n'était pas le plus cher.** Une image de glissé de note coûtait 26 ms, dont l'essentiel ailleurs : chaque commande faisait `refresh()` sur les 17 tranches du mixer (combos vidées et reremplies), et la playlist se repeignait entière. |

## 2. Les temps de repeint, avant et après chaque étape

Release, 95e centile, en ms. « D2D » : repeint dans une image Direct2D, le moteur de la fenêtre ; « logiciel » : le
moteur du mode léger. Un glissé : une image = le mouvement de la main, la réponse des panneaux au changement du
projet, et le repeint réel de la région salie (moteur logiciel, dont le peer peint quand on le lui demande).

| Mesure | Départ (A) | B | C | D | E | F | G |
|---|---|---|---|---|---|---|---|
| Playlist entière, D2D | **24,95** | 19,26 | **5,03** | 5,37 | 4,85 | 4,76 | **4,79** |
| Playlist entière, logiciel | 21,63 | 16,30 | 2,37 | 3,17 | 2,69 | 2,35 | 2,46 |
| Piano-roll entier, D2D | 27,26 | 5,18 | 4,93 | 5,54 | 5,05 | 5,26 | 5,18 |
| Piano-roll entier, logiciel | 27,85 | 3,17 | 3,29 | 3,60 | 2,93 | 3,00 | 3,07 |
| Mixer entier, D2D | 8,38 | 6,67 | 6,67 | 7,87 | 6,88 | 6,65 | 7,25 |
| Mixer entier, logiciel | 6,53 | 6,20 | 5,21 | 5,73 | 5,39 | 4,93 | 5,33 |
| Image de déplacement de fenêtre interne, D2D | **65,36** | 29,29 | 6,41 | 6,69 | 6,51 | 6,76 | **6,55** |
| Glisser un bloc, par image | — | — | — | 6,18 | 0,96 | 1,01 | 1,18 |
| Bande de sélection de la playlist, par image | — | — | — | 6,27 | 0,61 | 0,89 | 1,15 |
| Glisser une note, par image | — | — | — | 26,35 | 8,88 | 7,10 | 9,77 |
| Bande de sélection du piano-roll, par image | — | — | — | 9,34 | 1,93 | 1,65 | 2,13 |

- La colonne D des glissés est mesurée après D, avant E, avec la mesure des glissés ajoutée pour cela (`8a5d405`).
- D ne touche aucune peinture : ses écarts sont du bruit, un `--verify` complet tournant en parallèle.
- Le glissé de note, en E : 26,35 → 22,25 avec la seule union des rectangles, 12,45 avec le mixer, 8,88 avec la
  playlist qui ne repeint que les blocs du pattern touché. Il reste ~3 ms de réaction des panneaux et le repeint,
  sous le piano-roll, de ces blocs.
- La tête de lecture, en lecture, playlist zoomée : avant F, un minuteur de 33 ms (30 déplacements par seconde
  par construction) ; après F, **129 déplacements pour 141 images** en 2,42 s. En léger : 28,5 par seconde.
- **Limite de la mesure :** le chrono Direct2D s'arrête quand le contexte est vidé vers le GPU, pas quand le GPU a
  fini. Ce qui fait saccader est le temps du fil de messages, et c'est lui qui est mesuré.

## 3. Ce qui a été fait, étape par étape

- **A** (`25cce40`) — `--verify-fluidite` : projet vide rempli par le bus (16 pistes, 8 patterns, 40 blocs, 200 notes
  ouvertes au piano-roll, playlist, mixer et piano-roll ouverts), repeints réels chronométrés, médiane, 95e
  centile, pire au rapport. `a5f221c` : la mesure rend les pages comme elle les a trouvées (voir §6).
- **B** (`3c613b1`) — `TokenTable` : `tokens.json` aplati au chargement, chemin vers valeur décodée, recherche sans
  allocation. API publique de `Tokens` inchangée ; token inconnu : même `jassert`.
- **C** (`700b0f3`) — `setOpaque(true)` sur les 17 composants qui remplissent tout leur rectangle. La playlist peint
  depuis un cache (blocs, lignes, libellés, fin du morceau) reconstruit quand la révision du projet change :
  `ProjectObserver` compte les changements au moment où le bus les rapporte, donc jamais une image de retard, et
  jamais sur un défilement ou un zoom. Les blocs hors de la zone de clip ne sont plus visités.
  **Pas opaques :** les lignes de la liste des pistes et de la chaîne de plugins (fond seulement survolées ou
  sélectionnées), le vumètre du mixer (liserés d'un pixel entre ses barres), `AppShellView` (ne peint rien), les
  boutons (coins arrondis de la look-and-feel).
- **D** (`c1d2ec5`) — `TitleBarView::findControlAtPoint` : légende hors de ses contrôles, réduire, agrandir,
  fermer sur ses boutons. `ComponentDragger` retiré. Double-clic : agrandit, par Windows.
- **E** (`0eba76c`, `ef6b07f`) — bloc, bande, note : l'union d'avant et d'après. Point d'automation : sa ligne,
  parce que les segments de part et d'autre bougent avec lui. Mixer : une tranche qui montre déjà ce que dit le
  projet ne se rafraîchit plus. Playlist : si seules des notes ont changé (`note.*`), seuls les blocs des patterns
  dont l'aperçu a été reconstruit sont repeints.
- **F** (`7b8a2ac`) — `FrameTicker`, un `VBlankAttachment`, à la place des minuteurs de la playlist, du piano-roll,
  du transport (qui ne repeint plus que ses deux afficheurs), du mixer et de la liste des pistes.
  `TransportClock::displayBeats` : la position à dessiner, portée en avant du temps écoulé depuis le dernier bloc
  audio, au plus 50 ms, jamais en arrière sauf saut réel du moteur. La logique lit toujours `positionBeats`.
- **G** (`0bfcd41`, `edbacb3`) — la règle d'hygiène n°1 voit les durées littérales (`startTimer`, `startTimerHz`,
  `withDurationMs`, `fadeIn`, `fadeOut`, `animateComponent`). Fichier > Affichage > Fluide / Léger : réglage de la
  machine, dans les réglages de l'application (AppData), jamais dans le projet. Léger : 30 images par seconde
  (`motion.light.framesPerSecond`) et le moteur logiciel de JUCE sur chaque fenêtre, fenêtres de plugin comprises.

## 4. Ce qui est tombé

- **Les animations du mode fluide.** Par méthode : la liste est exposée en §9 et attend ton accord.
- **Le glissé d'un point d'automation n'est pas mesuré.** Le code limite son repeint à sa ligne ; la mesure demande
  une ligne d'automation à l'écran dans le projet de mesure, pas faite.
- **144 Hz n'est pas mesuré** : cette machine a un écran 60 Hz. Le mécanisme ne dépend pas de la fréquence (une image
  = un appel), mais c'est à voir à la main.
- **Les vumètres restent à 30 Hz.** Les panneaux sont calés sur l'écran, mais `LevelMonitor` lit les prises du
  moteur à 30 Hz. Les faire retomber en douceur est dans la liste des animations (§9).
- **Clang n'a pas été compilé ici** (Windows seul sur cette machine) ; la CI le fera au push.

## 5. La règle du test cassé une fois

Chaque vérification neuve a été cassée une fois dans le code qu'elle regarde ; toutes sont passées au rouge.

| Vérification | Cassure | Résultat |
|---|---|---|
| Chaque token donne la valeur d'avant | l'alpha de `#RRGGBBAA` laissé en place | rouge |
| Un défilement ne reconstruit pas le cache | le cache invalidé dans `setView` | rouge (3 reconstructions) |
| La barre répond à Windows comme une barre de titre | la légende répondue `client` | rouge |
| Les aperçus reconstruits sont nommés | la liste jamais vidée | rouge |
| Un déplacement de la tête de lecture par image | sans la position portée en avant | rouge (106 pour 139) |
| Léger : moteur logiciel | le moteur non appliqué | rouge |
| Règle d'hygiène des durées | une sonde avec les trois formes | trois violations |

Une vérification était aveugle au premier essai, trouvée par son chiffre et non par une cassure : le glissé de
note mesurait 0 ms. La note, à la largeur du pattern, était plus étroite que sa poignée d'allongement : la main
l'allongeait au lieu de la déplacer. Le piano-roll est zoomé avant la mesure.

## 6. Vérifications de l'application

`--verify` complet, sans copilote (`--no-copilot`), sur le code d'avant la semaine (`bc5fe4e`) et sur celui de C :
**510 passées / 38 en échec contre 509 / 39.** Les 38 sont les mêmes, une pour une : le copilote absent et ce qui
en découle (le Ctrl+Z qui devait défaire son fondu défait autre chose, et les comptes d'historique qui suivent
glissent). Le 39e, « la page Navigateur est ouverte », venait de moi : ma mesure fermait le navigateur, et la
machine s'en souvenait. Corrigé (`a5f221c`), et la disposition mémorisée remise comme le dit le manifeste
(navigateur ouvert, mixer fermé) dans `%APPDATA%\DAW IA\DAW IA.layout`.

**Sur le code final, une régression de D, trouvée et corrigée :** l'étape « double-clic sur la barre : agrandir »
envoyait un double-clic JUCE à `TitleBarView`, qui ne le traite plus — c'est Windows qui agrandit. L'étape d'après,
« le second double-clic rend la taille », passait sans rien prouver : la fenêtre n'avait jamais été agrandie. Les
deux passent maintenant par le vrai chemin (double-clic sur la légende envoyé à la fenêtre), `9836d1c`.

**`--verify` sur le code final (`9836d1c`) : 510 passées, 38 en échec, les mêmes que la référence, une pour une.**

**Un incident de mon fait.** Les rendus WAV et les captures de mes passages `--verify` ont rempli le disque (6 Mo
libres) ; un passage s'est arrêté sans rapport, et le journal partagé montre trois « project not saved: database or
disk is full ». Ils peuvent venir de mes processus comme de ton instance ouverte. J'ai supprimé mes fichiers de
travail (2,5 Go libres). Regarde la barre de titre de ton projet ouvert : si elle dit « non enregistré »,
« Enregistrer » le règle maintenant que le disque a de la place.

`--verify-file` : vert, avec les deux étapes neuves de la barre de titre. `--verify-fluidite` : 27 passées, 0 en
échec.

## 7. Les écarts à l'acquis, dits

- **`findControlAtPoint` est aussi surchargé dans `MainWindow`.** Le brief le mettait dans `TitleBarView` seul ; JUCE
  ne pose la question qu'au composant de bureau. `MainWindow` descend jusqu'au composant sous le point, par
  rectangles seulement (demander `contains()` au système depuis son propre test boucle), et remonte au premier qui
  n'est pas `client`.
- **Une fenêtre agrandie qu'on tire se restaure et suit la main,** comme toute fenêtre Windows. L'ancienne règle
  « agrandie, elle ne se glisse pas » et son étape de vérification sont tombées.
- **La tête de lecture dessinée peut devancer le moteur de 50 ms au plus.** C'est une position d'affichage, séparée
  de celle que lit la logique.
- **La playlist lit le type des commandes** (`note.*`) pour savoir que seules des notes ont changé. C'est une
  convention de nommage devenue une règle d'écran : une commande `note.*` qui toucherait autre chose que des notes
  laisserait la playlist en retard d'un repeint. Vérifié aujourd'hui pour les sept.
- **`ProjectObserver` compte les révisions** et retient la dernière qui n'était pas une note : un état d'écran, pas de
  domaine.
- **Le réglage Fluide / Léger partage le fichier de la disposition** (`DAW IA.layout`) au lieu d'un fichier à lui.
  Même raison d'être là : ce sont les réglages de cette machine.
- **La vérification reçoit `ProjectObserver`** pour distribuer son message dans une image mesurée.
- **Ton travail de toile en cours** (quatre fichiers non commités de la playlist) a été rangé sur une branche
  locale, `wip/toile-s18` (`740fa31`), pour que ce chantier parte d'un `main` propre. Il n'est ni perdu ni poussé ;
  il faudra le rebaser sur `main` : `PlaylistPanel` a changé sous lui (cache, `frame()`, repeints limités).

## 8. À essayer à la main, dans l'ordre

Sur `main`, en Release de préférence (le Debug est lent partout).

1. **La fenêtre principale.** Tire-la par la barre, vite, en rond : elle doit suivre comme une fenêtre de
   l'Explorateur. Tire-la contre le bord gauche, puis dans un coin : ancrage. Survole le bouton □ : les
   dispositions de Windows 11 doivent apparaître. Double-clic sur la barre : agrandie ; tire-la : elle se
   restaure sous la main. Vérifie que « Fichier » et les boutons d'espace de travail répondent toujours au clic.
2. **Une fenêtre interne.** F7, puis tire le piano-roll au-dessus de la playlist remplie : pas de trace, pas de
   retard. Même chose avec le mixer (F10), qui est la plus lourde à repeindre.
3. **Glisser des blocs.** Sélectionne cinq blocs, glisse-les de ligne en ligne ; Ctrl + glisser une bande sur la
   playlist, puis sur le piano-roll ; glisse une note, piano-roll et playlist visibles ensemble : le bloc du
   pattern doit suivre la note sans que le reste clignote.
4. **La lecture.** SONG, lecture, playlist zoomée (Ctrl + molette) : la tête de lecture doit glisser, sans à-coups.
   Si tu as un écran 144 Hz, mets la fenêtre dessus : c'est là qu'il faut regarder.
5. **La bascule.** Fichier > Affichage > Léger : l'interface doit rester juste, sans redémarrer ; la tête de
   lecture avance plus sèchement (30 images par seconde). Ferme, rouvre : toujours léger. Repasse en Fluide.

## 9. Les animations du mode fluide, à valider avant d'être codées

Toutes coupées en léger. Chaque durée est un token `motion.*`, la règle d'hygiène les voit.

| # | Animation | Comportement proposé | Token proposé |
|---|---|---|---|
| 1 | La page qui tourne sous la tête de lecture (playlist) | Quand la tête de lecture sort de la vue, la vue défile jusqu'à la page suivante au lieu de sauter, en ralentissant à l'arrivée. La tête de lecture reste dessinée à sa vraie place pendant le défilement. | `motion.duration.page` : 180 ms |
| 2 | Le zoom amorti (playlist et piano-roll) | La molette fixe une cible ; le zoom la rejoint en ralentissant, ancré sous le pointeur ; un cran de plus repart de là où il en est. Le défilement sans zoom reste immédiat. | `motion.duration.zoom` : 120 ms |
| 3 | Les vumètres qui retombent (mixer, liste des pistes) | Montée instantanée, descente à vitesse fixe ; la crête tient un instant puis retombe au même rythme. Les valeurs arrivent toujours à 30 Hz : la descente est interpolée à chaque image. | `motion.meter.fallDbPerSecond` : 24 ; `motion.duration.peakHold` : 800 ms |
| 4 | Les fenêtres internes en fondu | À l'ouverture seulement (F5–F10, onglets), de transparente à opaque. La fermeture reste immédiate : une fenêtre qui s'attarde reçoit encore des clics. Les fenêtres de plugin, natives, n'en ont pas. | `motion.duration.panel` : 200 ms (existe) |

Rien d'autre : pas de survol animé, pas de transition d'espace de travail, pas de fondu de la barre d'état. Dis-moi
ce que tu gardes, ce que tu changes, et les durées.

## 10. Reste à faire

- **Pousser.** Les 12 commits et ce bilan sont locaux ; `main` était déjà en avance d'un commit (`bc5fe4e`) sur
  `origin`. J'attends ton accord pour pousser, et la CI (Clang sous Linux) dira ce que je n'ai pas pu compiler ici.
- Ta validation des animations (§9), puis leur code.
- Tes essais à la main (§8), surtout le 144 Hz.
- Rebaser `wip/toile-s18` sur `main` avant de reprendre la toile.
- Le mixer entier reste le repeint le plus lourd (~7 ms en D2D) : à regarder s'il gêne à l'œil.
