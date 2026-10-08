# Essais après la S26

La phase d'essais et d'ergonomie vient après la S26 (décision du 3 octobre 2026) : les 26 semaines servent à
développer, et aucune n'attend un essai du fondateur. Ce fichier rassemble ce qui sera essayé à la main et à
l'oreille, dans l'ordre des bilans.

Chaque section est recopiée de son bilan telle qu'elle a été écrite, sans rien en retirer. Certaines étapes
nomment des écrans qui ont changé depuis (le piano-roll, la playlist avant la toile) : la phase d'essais dira
lesquelles ont encore un sens. Chaque bilan à venir ajoute la sienne à la fin.

## S17 — les lignes, la zone multi-pistes

### À essayer et à écouter, dans l'ordre

Sur `main`, compilée chez toi, avec carte son et clé d'API.

1. **Un projet d'avant.** Ouvre un projet de la S16. Les lignes doivent être celles que tu avais, dans le même
   ordre. Lance la lecture : rien ne doit avoir changé à l'oreille.
2. **Glisser vers le bas.** Prends un bloc, descends-le sur « + Nouvelle ligne » : une ligne naît. Ctrl+Z :
   tout revient. Recommence avec trois blocs sélectionnés.
3. **Ranger à la main.** Double-clique le nom d'une ligne, appelle-la « Drums » ; glisse son nom tout en haut.
   Ctrl+C / Ctrl+V un bloc : la copie doit rester sur sa ligne.
4. **La zone.** Trois canaux (Keys, 808, Pluck), trois lignes nommées Accords, Basse, Mélodie. Alt + glisser
   sur les trois, quatre mesures ; « ✦ Générer » ; « une boucle trap en fa# mineur », Entrée.
5. **L'écoute de la zone.** Ctrl+Espace : les trois parties ensemble. **C'est le test le plus important de la
   semaine :** la basse suit-elle les accords à l'oreille ? Change de variante (flèches) : tout doit bouger
   ensemble.
6. **Tab,** puis Ctrl+Z : trois blocs apparaissent, puis disparaissent d'un coup.
7. **Le nom par le preset.** Un canal sur un VST3 dont le preset s'appelle « … Bass … », nom par défaut :
   « → Basse » doit apparaître en gris dans le channel rack. Dis-moi quels synthés annoncent leur preset.

## S18 — la toile, la grammaire

### À essayer, dans l'ordre

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

## S18 bis — la fluidité

### À essayer à la main, dans l'ordre

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
5. **Les animations.** Zoom à la molette sur la règle : il glisse. Lecture en SONG, zoomé : la page suivante
   arrive en glissant. F10 en lecture : les vumètres retombent en douceur, la crête tient un instant. F9 : la
   fenêtre des pistes apparaît en fondu.
6. **La bascule.** Fichier > Affichage > Léger : l'interface doit rester juste, sans redémarrer ; la tête de
   lecture avance plus sèchement (30 images par seconde), rien ne glisse ni ne fond. Ferme, rouvre : toujours léger. Repasse en Fluide.

Restés dans le « Reste à faire » du même bilan :

- Regarder les quatre animations à l'œil, surtout la page qui tourne et la chute des vumètres, et régler les
  tokens (`motion.*`, bilan S18 bis §9).
- Les essais à la main ci-dessus, surtout le 144 Hz.

## S19 — la toile remplace le piano-roll

### À essayer, dans l'ordre

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

## S20 — le mixage par l'IA

### À écouter, dans l'ordre

Sur `main`, Release et Debug recompilés dans ton dossier de build. Ferme les autres DAW IA. Ta clé dans
`DAW_IA_ANTHROPIC_API_KEY` pour que le modèle décide ; sans clé, ce sont les règles, et l'état le dit.

1. **Ton premier morceau.** Ouvre-le, F10, « Mixer ». Regarde la progression : tu peux jouer le morceau pendant.
2. **Lis la proposition** : une ligne par tranche, « garder » allumé, la phrase de chaque réglage. Une phrase qui
   te semble fausse, note-la : elle cite une mesure, je peux la vérifier.
3. **« Avant », puis « Après »**, au même endroit du morceau : les deux sont au même niveau, tu juges le mix,
   pas le volume. Bascule plusieurs fois sur le refrain.
4. **Décoche une tranche** dont tu ne veux pas le réglage, puis « Garder ». Une seule entrée « Mixage par l'IA »
   dans l'historique ; survole-la : les phrases. **Ctrl+Z** : tout revient.
5. **Les axes** : « Mixer » de nouveau (la mesure est reprise, c'est immédiat), pousse « percutant », relâche :
   une autre proposition. Écoute.
6. **La référence** : « Référence… », un morceau fini que tu aimes, la part à moitié, « Mixer ».
7. **Au copilote** : « mixe le morceau, plus large ».
8. **Tes deuxième et troisième morceaux**, 1 à 3. Puis donne-moi, pour chacun : mieux / pareil / moins bien à
   niveau égal, et ce que tu as décoché. `daw.log` garde les mesures avant/après de chaque essai.

### Les critères d'écoute de la S20

Ils demandaient l'oreille du fondateur ; ce ne sont plus des dettes de développement. Recopié du bilan S20 §4 :

**Pas fait : il faut tes morceaux.** Ce que je te demande au §11 donne exactement ces nombres ; je remplirai
ce tableau avec ce que tu me rapportes (ou ce que montre `daw.log`, qui écrit chaque étape du mixage).

| Morceau | Master avant (LUFS / dBTP) | Master après | Kick/basse : marge du kick sur ses coups | Garde-fous franchis | À l'oreille, à niveau égal |
|---|---|---|---|---|---|
| 1 | | | | | |
| 2 | | | | | |
| 3 | | | | | |

Sur les signaux de `--verify-mix` : master −7,94 LUFS / +1,75 dBTP avant, −14,21 LUFS / −2,65 dBTP après
l'essai ; aucun garde-fou franchi ; le kick passe la basse à 63 Hz de 1,3 dB avant, 9,5 dB après, sur ses
coups ; entendus au même niveau à 0,02 dB près (RMS de ce qui est sorti par la carte son).

**Le recouvrement kick/basse, tel que mesuré, ne baisse pas sur ces signaux** (40 % → 40 %). Le critère (deux
octaves à 6 dB l'une de l'autre au même moment) ne voit pas le niveau d'un son qui décroît : un kick traverse
à chaque coup le niveau de la basse, quel que soit ce niveau. Ce que la phrase annonce (« pour laisser passer
le kick ») est vérifié autrement : la marge du kick sur la basse, sur le cinquième des instants où le kick est
le plus fort. Le critère de succès « le recouvrement baisse » reste à juger sur tes morceaux ; c'est une dette.

Noté le 3 octobre 2026 : en S21, ce critère est remplacé par la marge du kick sur ses coups, mesurée par la
machine. Le tableau ci-dessus reste à remplir à l'oreille, sur les trois morceaux du fondateur.

Le coût réel d'un mixage (bilan S20 §5) n'est pas un essai : il est mesuré par la machine en S21.

## S21 — la carte son, les fenêtres, le mixage par le modèle

### À essayer, dans l'ordre

Sur `main`, Release recompilé. Ta clé dans `DAW_IA_ANTHROPIC_API_KEY`.

1. **Les AirPods qui partent.** Lance la lecture sur tes AirPods, puis range-les dans leur boîte. Le son doit
   passer sur les haut-parleurs dans la seconde, et « sortie : Haut-parleur… » s'afficher sous la position.
   Ressors-les : le son doit revenir dedans. Dis-moi si basculer tout seul sur les haut-parleurs te gêne.
2. **L'avant/après sur les haut-parleurs du portable** : F10, « Mixer », « Avant », « Après ». Ça doit
   s'entendre.
3. **Les fenêtres.** Glisse l'historique contre le rack : il doit se coller. Tire le bord qu'ils partagent :
   les deux bougent. Dis-moi si 10 px de magnétisme est trop ou trop peu.
4. **Le rack long.** Une vingtaine de canaux : la molette, le clic-molette, F sur le canal choisi.
5. **L'historique.** Descends-le, fais une action : ce que tu regardais ne doit pas bouger. Remonte en haut :
   il suit.
6. **Fermer**, en lecture, copilote ouvert : le processus doit disparaître (gestionnaire des tâches).

## S22 — les stems, la direction par références

### À essayer, dans l'ordre

Sur `main`, Release recompilé par `scripts/build.cmd`. Ta clé dans `DAW_IA_ANTHROPIC_API_KEY`.

1. **Séparer une vraie chanson.** Pose un morceau sur la playlist, Alt+clic droit, « Séparer en stems
   (rapide) ». La première fois, l'installation et le téléchargement des poids prennent du temps. Compte environ
   la durée du morceau pour le calcul. Écoute chaque stem seul : dis-moi où ça bave (la voix dans le reste, la
   basse dans la batterie).
2. **« Annuler » pendant la séparation** : rien ne doit rester, ni piste ni fichier.
3. **La meilleure qualité** sur le même morceau (environ quatre fois plus long) : l'écart s'entend-il assez pour
   la proposer ?
4. **Une référence.** F11, ajoute un morceau que tu connais : le tempo et la tonalité lus sont-ils justes ? Les
   sections tombent-elles aux bons endroits ?
5. **Deux références qui ne s'accordent pas** sur le tempo : la contradiction doit être dite, le tempo laissé
   vide jusqu'à ce que tu le tapes.
6. **Le mixage vers la référence** : F10, « Mixer », part de direction à fond puis à zéro. Écoute l'avant/après
   à niveau égal.
7. **Générer avec une référence** : un pattern vide, Ctrl+G. Les notes doivent être dans la tonalité de la
   référence, et plus denses si sa voix joue tout le temps.
8. **Demander au copilote** de « suivre la structure de la référence » : dis-moi ce qu'il fait des sections.
   C'est la question du §5 du bilan S22.

## S23 — jouer au clavier, enregistrer

### À essayer, dans l'ordre

Sur `main`, Release recompilé par `scripts/build.cmd`.

1. **`--verify-lecture`, puis `--verify-lecture --sans-jeu`**, dossier et disposition jetables. C'est une
   vérification, pas un essai, mais elle ne tourne que sur ta carte : si le jeu fait des lectures muettes et
   `--sans-jeu` non, dis-le-moi avant tout le reste.
2. **`--verify-jeu`** sur ta carte. Note la latence que le rapport donne (carte, tampon, de la touche à l'oreille).
3. **Le clavier de l'ordinateur.** Choisis un canal dans le rack, Ctrl+T, joue W S X D C… Les notes tombent-elles
   sous les doigts comme dans FL ? Change d'octave avec ← et →. Essaie un accord de trois notes : ton clavier les
   passe-t-il toutes ?
4. **Ctrl+T allumé, tape dans le copilote** : du texte, aucune note.
5. **Change de canal en tenant une touche**, puis relâche : la première piste doit se taire.
6. **Un clavier MIDI.** Branche-le pendant que le logiciel tourne : la ligne du transport doit le nommer dans la
   seconde. Joue, pédale comprise. Débranche-le en tenant un accord : silence, et le logiciel ne plante pas.
   Rebranche : il rejoue.
7. **Enregistre en PAT.** Ctrl+R, la mesure de décompte, joue quatre noires sur un pattern d'une mesure, plusieurs
   passes. Les notes doivent arriver en rouge, s'ajouter à chaque passe, et être écrites à ● ; un Ctrl+Z retire
   toute la prise. Sont-elles sur le temps, à l'oreille ? Si elles sont en retard ou en avance d'une même
   quantité, dis-le : c'est la latence que ta carte déclare qui ment.
8. **Enregistre en SONG**, au milieu du morceau : un pattern neuf doit apparaître là où tu as commencé.
9. **La sensation.** Joue vite, une gamme : la latence se sent-elle ? Si oui, le tampon par défaut de Windows est à
   baisser, et la fenêtre « Audio » tombée cette semaine devient la priorité.

## S24 — la fenêtre « Audio », le flux audio, le kit, les bus intelligents

### À essayer, dans l'ordre

Sur `main`, Release recompilé par `scripts/build.cmd`.

1. **F12, « Tester ma carte ».** Ta Realtek, puis un casque Bluetooth et, si tu en as une, une autre carte. Le
   conseil est-il un réglage où tu ne sens pas la latence en jouant ? Change de tampon pendant que le morceau joue :
   rien ne doit s'arrêter.
2. **Le mixer, les emplacements d'effets.** Pose un égaliseur, un de tes plugins, glisse-les, contourne-les.
   Est-ce plus rapide que la page des plugins ? Garderais-tu les deux ?
3. **F3, le flux audio**, sur un vrai morceau à toi. Comprends-tu le routage en le regardant ? Le graphe est-il
   lisible à 15 pistes, ou faut-il replier ? Clique un égaliseur : l'avant et l'après font-ils comprendre ce qu'il
   fait ?
4. **Écoute seul** la sortie d'un bus, puis l'avant d'un effet : est-ce le geste qui fait comprendre ?
5. **Glisse un effet sur un lien, tire un envoi vers un bus** : les gestes se trouvent-ils sans qu'on te les dise ?
6. **« Mixer » avec F3 ouvert** : la proposition dans le graphe aide-t-elle à décider de garder ?
7. **La page « Kit »** sur ta bibliothèque. Combien de temps prend la première mesure ? Les rôles sont-ils justes ?
   Le kit sonne-t-il ensemble à l'écoute ? Bouge les axes : le kit change-t-il dans le sens attendu ?
8. **La page « Bus »** sur un projet où plusieurs pistes portent ta réverbération : la proposition est-elle juste,
   et la phrase sur le son sec, si elle vient ?

## S25 — diriger le DAW à la voix

### À essayer, dans l'ordre

Sur `main`, Release recompilé par `scripts/build.cmd`. Les poids sont déjà sur ta machine.

1. **Tiens Ctrl droit et dis « Baisse la caisse claire de trois décibels »** sur un projet qui a une caisse claire.
   La phrase arrive-t-elle juste ? Le délai de 1,5 s se lit-il, et laisse-t-il le temps de lire ?
2. **Tes phrases de tous les jours**, avec tes mots anglais et tes noms de pistes (« mets un compresseur sur la
   808 », « passe en fa dièse mineur à 140 », « duplique le pattern trois »). Combien partent justes ? Combien
   attendent alors qu'elles étaient justes ? Si trop attendent, le seuil de 0,6 descend.
3. **Enregistre le jeu d'essai avec ta voix** (`services/tests/voix/phrases.tsv`, un fichier WAV 16 kHz mono par
   phrase, même nom) : `voix_essai.py record` puis `report` donnent ton vrai taux d'erreur.
4. **Parle pendant que le morceau joue**, sur les haut-parleurs puis au casque. La baisse de 20 dB gêne-t-elle ?
   Suffit-elle sur les haut-parleurs ?
5. **Avec tes AirPods sur les oreilles**, parle : le son des AirPods doit rester le même pendant que le micro de
   l'ordinateur écoute. Choisis ensuite le micro des AirPods dans F12 pour entendre ce que l'avertissement annonce.
6. **« Supprime la piste guitare » dite** : la confirmation est-elle de trop, ou bienvenue ?
7. **Une phrase douteuse** : la raison affichée aide-t-elle à corriger ? Le mot proposé (« est-ce Charleys ? ») est-il
   le bon ?
8. **Les instruments IA** : écoute les démos dans l'ordre de `docs/evaluation-instruments-ia.md` §2.
