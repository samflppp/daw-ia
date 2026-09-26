# Bilan de fin de S11 — DAW IA

**Période :** semaine 11 sur 26. Rédigé le 26 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 12 commits S11 (`714f05a` → ce bilan), CI verte sur
chaque commit terminé (un passage annulé parce que j'ai poussé pendant qu'il tournait, le suivant le couvre).
**Volume :** 78 fichiers, +8 251 lignes, −139 avant ce bilan (rapports de vérification compris).
**Tests :** 258 cas ctest (domaine, persistance, interface), 70 cas d'engine (rendus audio), 19 cas Python.
Zéro warning.
**Vérifications par l'application :** 212, aucune en échec (§10).
**Registry :** 47 types de commandes, contre 42. Les 5 nouvelles sont dans la table du copilote, test croisé
passé dans les deux sens.
**Schéma de projet :** 4, inchangé. Un projet sans mixage s'écrit à l'octet près comme avant ; un test le
vérifie.

## En bref

Tous les points de la semaine sont livrés, dans l'ordre de priorité que tu avais donné : vu-mètres, zoom
de la playlist, aperçus et formes d'onde, modèle de mixage, presse-papiers étendu, écoute du navigateur. Rien
n'est tombé. Deux choses sont livrées moins loin que ce que tu pourrais attendre, et je le dis au §12 : la
page Mixer ne montre qu'un envoi par tranche, et les inserts d'un bus ne se posent pas encore depuis l'écran.

Les vu-mètres ont servi avant même d'avoir un écran : en mesurant, ils ont trouvé quatre défauts qui dataient
des semaines précédentes (§3). Le plus sérieux : **tous les rendus des vérifications depuis la S10 tournaient
pendant que le périphérique audio tenait le même graphe**, et Tracktion l'écrivait dans le journal à chaque
fois.

**Ce que le copilote a pour mixer :** les douze verbes sont là. **Il lui manque les moyens de juger** : onze
besoins, surtout des mesures (§7).

## 1. Livrables

| # | Livrable | État | Où |
|---|---|---|---|
| 1 | Ctrl+C / V / B et sélection multiple dans le piano-roll et le rack | Livré | §8 |
| 2 | Le presse-papiers exposé et validé avant de coder | Livré | §2 |
| 3 | Défilement et zoom de la playlist | Livré | §4.1 |
| 4 | Écouter un sample dans le navigateur | Livré | §9 |
| 5 | Aperçu des notes dans les blocs | Livré | §4.2 |
| 6 | Forme d'onde des clips audio | Livré | §4.3 |
| 7 | Le modèle de mixage exposé et validé avant de coder | Livré | §2, §5 |
| 8 | Vu-mètres, crête et RMS, lisibles par le copilote | Livré, prouvés au rendu | §3 |
| 9 | Page Mixer (F10), solo comme commande | Livré, un envoi visible par tranche | §6 |
| 10 | Bouton « Mixer par l'IA » : test de complétude | Livré | §7 |
| 11 | Nouvelles commandes dans la table du copilote | Livré, 47 = 47 | §5 |

## 2. Les deux décisions validées avant de coder

**Le presse-papiers vit dans l'interface.** Ce n'est pas un état du projet : il n'est ni journalisé, ni annulé,
ni enregistré. Dans le domaine, il faudrait une commande, et Ctrl+C entrerait dans l'historique. Le copilote
n'en a pas besoin : il écrit déjà les notes par valeur. Ce qui est copié est **du contenu**, des notes par
valeur (hauteur, vélocité, longueur, début relatif à la copie), jamais des identifiants. La copie survit donc
à un changement de pattern, et même à la suppression des notes d'origine.

**Le mixage : bus et master sont des `Track`**, rangés à part des canaux (`tracks()`, `buses()`, `master()`).
Deux fonctions séparent les rôles :
- `findTrack` ne connaît que les canaux. Aucune ligne de pattern, aucun sample, aucun clip audio ne peut donc
  tomber sur un bus.
- `findStrip` connaît les trois. Volume, pan, coupure, nom et plugins s'appliquent au bus et au master sans
  nouvelle commande et sans changer un seul payload épinglé.

L'autre option, des entités `Bus` et `Master` à part, aurait obligé à changer le payload de `plugin.insert`.
Le solo est calculé une fois, dans le domaine (`ProjectState::isAudible`), et la projection le lit.

## 3. Les vu-mètres

**La mesure.** `MeterTapPlugin` est un plugin interne, placé par le projecteur **en dernier** sur chaque piste
Tracktion et sur le master. Il mesure donc ce qui sort de la tranche : après les effets, le fader et le pan.
- **Dans le callback :** par bloc, crête et somme des carrés, écrites dans un anneau pré-alloué avec deux
  indices atomiques. Ni allocation, ni verrou.
- **Pourquoi pas le mesureur de Tracktion :** son `LevelMeasurer` prend un spin lock dans le callback et peut
  y agrandir un vecteur.
- **Sur le thread message :** `LevelMeters` lit ces anneaux 30 fois par seconde et garde une fenêtre de
  300 ms (crête tenue, RMS). Il garde aussi des totaux depuis une remise à zéro, pour mesurer un rendu.

**Pour le copilote :**
- `mix.levels` (JSON-RPC) rend, par piste et pour le master, crête et RMS en dBFS (gauche, droite et
  ensemble), au dixième de dB, avec l'indicateur de saturation.
- L'outil de lecture s'appelle `mix.get_levels`.
- Le prompt lui dit qu'à l'arrêt tout est à -100 dB, et qu'un niveau se mesure au lieu de se déduire d'un
  volume.

**La preuve, sur un rendu, jamais en lisant un champ.** Un rendu est comparé au fichier WAV qu'il produit :
- la même crête, à 0,1 dB près ;
- la même énergie (et non le même RMS, parce que le rendu traite une seconde de pré-roll silencieuse) ;
- un fader baissé de 6 dB baisse le vu-mètre et le fichier de 6 dB ;
- une piste coupée mesure -100 dB ;
- un signal au-delà de 0 dBFS allume le voyant de saturation.

**Trouvé en mesurant, et corrigé :**

| Défaut | Depuis | Effet |
|---|---|---|
| Le fader master de Tracktion ne part pas de 0 dB | toujours | tout le mix sortait 3 dB plus bas que ce que les pistes envoyaient. Le projecteur l'écrit maintenant à l'unité |
| `Renderer::renderToFile(Edit&, …)` passe par le chemin du gel, qui **démute toutes les pistes** | toujours | une piste coupée s'entendait dans les rendus ; aucune vérification ne rendait de mute |
| **Les rendus tournaient pendant que le périphérique audio tenait le même graphe** | S10, dans `--verify` | les mêmes plugins traités par deux threads à la fois. Tracktion l'écrivait (« Rendering whilst attached to audio device ») à chaque vérification. `engine::renderAsPlayed` libère le contexte de lecture le temps du rendu |
| Une piste muette n'est plus traitée du tout | — | la remise à zéro des totaux est demandée par le lecteur et faite par l'écrivain, et vaut zéro d'ici là |

**Trouvé et non corrigé, dit :** dans le harnais des tests moteur, la lecture s'arrête environ 2 s après le
démarrage. C'est le scan MIDI asynchrone de Tracktion (`applyNewMidiDeviceList`), qui reconstruit le graphe.
- Dans l'app, ce scan arrive une fois, au lancement.
- Les traces n'ont montré aucun arrêt automatique pendant les vérifications.
- Une personne qui appuie sur lecture dans les 2 premières secondes après l'ouverture pourrait donc voir la
  lecture s'arrêter une fois. Je ne l'ai pas reproduit dans l'app.

**Limite :** une piste qui joue à la fois un instrument et des clips audio occupe deux pistes Tracktion. Sa
mesure combine les deux : crête = la plus haute, RMS = somme des puissances. C'est exact quand une seule des
deux joue, le cas courant.

## 4. La playlist lisible

### 4.1 Défilement et zoom

- **Au repos**, la chanson tient dans la largeur, comme avant, mais jamais sous 6 px par temps (24 px par
  mesure, token `metric.playlist.beatWidthMin`). Au-delà, la timeline défile.
- **Les gestes :**

| Geste | Effet |
|---|---|
| molette | défile les pistes |
| Maj + molette, ou balayage latéral | défile la timeline |
| Ctrl + molette | zoom autour du pointeur ; le dézoom s'arrête à la largeur lisible |
| barres de défilement | ce qu'elles font toujours |

- **Au-delà de la fin :** la vue peut aller jusqu'à 999 mesures pour poser plus loin, comme dans FL. Un Ctrl+Z
  ne la déplace pas.
- **En SONG, pendant la lecture**, la page tourne quand la tête de lecture sort de la vue.
- **Performance :** seules les mesures visibles sont dessinées.
- **Sélection :** Ctrl + clic ajoute un bloc ou le retire, comme validé. Ctrl + Maj + clic reste un alias.

### 4.2 Aperçu des notes

Chaque bloc de pattern dessine les notes de son pattern : le temps en abscisse, la hauteur en ordonnée,
toutes lignes confondues, dans la plage de hauteurs du pattern.

**Performance.** `PatternPreviews` garde une géométrie par pattern.
- Elle n'est reconstruite que quand **l'empreinte des notes ou la longueur** du pattern change.
- Jamais au repeint, jamais pour une pose : un pattern posé huit fois n'est construit qu'une fois.
- Un repeint ne fait que mettre cette géométrie à l'échelle du bloc.
- La vérification compte les constructions dans l'app, en lisant le compteur une étape plus tard, parce que le
  projet notifie les panneaux de façon asynchrone :

| Geste | Aperçus reconstruits |
|---|---|
| déplacer un bloc | 0 |
| Ctrl+Z du déplacement | 0 |
| allumer une case du rack | 1, pour les huit poses |
| Ctrl+Z de la case | 1 |

**Seuil :** sous 24 px de large (`metric.playlist.previewMinWidth`), un bloc ne dessine rien de son contenu.
La hauteur des lignes passe de 28 à 34 px pour loger le nom et le contenu.

### 4.3 Forme d'onde

**Le calcul.** `WaveformPeaks` garde 400 paires min/max par seconde.
- Mesuré **une fois par empreinte**, sur un thread de `SampleLibrary`, directement depuis les octets du projet.
- Le sample n'est jamais décodé en entier : la mesure avance tranche par tranche.
- Le bloc s'affiche vide, puis se remplit.
- Au dessin, seules les colonnes visibles sont réunies.

**Ce que la vérification a mesuré :**
- la mesure de 120 s de sample, seule, prend 2 s ;
- pendant ce temps, le thread message n'est jamais resté plus de 184 ms sans tic (un tic toutes les 120 ms) ;
- dix dépôts du même sample coûtent une seule mesure.

**Ce qui fige encore, et vient d'ailleurs :** juste après le dépôt d'un sample de deux minutes, le thread
message reste jusqu'à ~300 ms sans tic. C'est le moteur qui reconstruit son graphe pour le nouveau clip,
comme à chaque dépôt depuis la S10, et non le calcul de forme d'onde.

## 5. Le mixage : domaine et projection

**Cinq commandes :**

| Commande | undoRecord | Coalescence |
|---|---|---|
| `bus.add {busId, name}` | l'identifiant | — |
| `track.set_output {trackId, output}` (vide = master) | l'ancienne sortie | — |
| `track.set_send {trackId, busId, levelDb}` | l'ancien niveau, ou « n'existait pas » : annuler le premier réglage retire l'envoi | par piste et par bus, sur le geste |
| `track.remove_send {trackId, busId}` | le niveau et le rang | — |
| `track.set_solo {trackId, soloed}` | l'ancien état | — |

**Retirer un bus :** c'est `track.remove`. Son undoRecord garde les pistes qui y allaient et les envois qu'il
recevait, et un Ctrl+Z rend l'état à l'octet près.

**Ce qui est refusé :**
- une route qui reviendrait à son point de départ ;
- une sortie ou un envoi vers autre chose qu'un bus ;
- un 33ᵉ bus : Tracktion en offre 32 ;
- retirer le master, ou le mettre en solo.

**La règle du solo** (`isAudible`) : une tranche en solo, ce qui l'alimente et ce qu'elle traverse restent
audibles. Le master sonne toujours, sauf s'il est coupé.

**Limite dite :** un bus de réverbe en solo garde ses sources audibles aussi en direct, sans l'effet. Couper un
chemin d'une piste sans couper l'autre demanderait un gain par route.

**La projection :**
- Un bus devient une piste Tracktion sans clip, avec un `AuxReturn` en tête, ses inserts, son fader et sa prise
  de niveau.
- Une sortie passe par `setOutputToTrack`.
- Un envoi est un `AuxSend` placé juste après le fader.
- Le master a notre fader, après ses inserts et devant sa prise de niveau.

**Prouvé au rendu, au dixième de dB :**
- un bus baissé de 6 dB baisse le rendu de 6 dB ;
- un envoi à -6 dB sur une piste à -3 dB arrive au bus à -9 dB, donc après le fader ;
- une piste coupée n'envoie rien ;
- en solo, la piste coupée par le solo mesure -100 dB, et le master égale le kick seul ;
- le fader et la coupure du master s'entendent dans le fichier.

**Trouvé au rendu :** la loi de pan « -3 dB au centre », bonne pour placer une piste mono, retirait 3 dB à
chaque passage par un bus. Un bus et le master prennent donc la loi linéaire : une balance, à l'unité au
centre. **Limite :** Tracktion n'a pas de vraie loi de balance, et la loi linéaire double un côté (+6 dB) en
pan extrême.

**Migration :** aucune. Chaque champ n'est écrit que s'il sert, et le schéma reste à 4.

## 6. La page Mixer (F10)

**Disposition :** une tranche par canal, puis par bus, le master à droite. « + Bus » crée un bus.

**Une tranche :**
- le nom ;
- les inserts, affichés ;
- un envoi (bus et niveau) ;
- la sortie ;
- le pan ;
- M et S ;
- le fader avec son niveau en dB ;
- un vu-mètre gauche/droite, avec crête tenue et voyant de saturation. Un clic sur le vu-mètre éteint les
  voyants.

**Comportement :**
- Une tranche qui ne s'entend pas, parce qu'elle est coupée ou exclue par un solo, a son nom grisé.
- Tout passe par le bus : un glissé de fader est une entrée d'historique.
- Déplacer un envoi d'un bus à un autre est un groupe, donc un seul Ctrl+Z.
- Une route refusée remet la liste déroulante sur ce qui est vrai.

## 7. « Mixer par l'IA » : ce qui manque au copilote pour mixer

Le bouton n'appelle aucun modèle. Il lance `mixingGaps` sur ce que le copilote peut atteindre, c'est-à-dire les
commandes de la registry et les méthodes JSON-RPC de l'app. Il affiche le résultat à la place des tranches ; le
même bouton les rend. La liste des besoins est écrite une fois, dans `MixingReadiness.cpp`, et **épinglée par un
test** : l'écart entre ce qui est offert et ce qui manque ne peut pas changer sans que ce test change.

**14 besoins sur 25 sont couverts.** Les verbes le sont tous : volume, pan, coupure, solo, bus, sorties,
envois, master, effets, paramètres, contournement, un Ctrl+Z par essai. L'état du mixage se lit, et les niveaux
instantanés aussi.

**Ce qui manque encore :**

| Besoin | Nom attendu | Pourquoi |
|---|---|---|
| connaître les paramètres d'un effet : noms, unités, plages, valeurs actuelles | `plugin.parameters` | l'état ne liste que les paramètres déjà touchés, par identifiant et en valeur normalisée : une IA ne sait pas que « 0,42 » est une coupure à 800 Hz |
| des effets de base connus quelle que soit la machine : EQ, compresseur, limiteur | `mix.stock_effects` | les plugins installés varient d'une machine à l'autre et leurs paramètres n'ont pas de sens commun |
| mesurer crête et RMS sur un passage choisi, par un rendu | `mix.measure` | 300 ms en direct ne disent rien d'un refrain. Les totaux d'un rendu existent dans le moteur (§3) mais ne sont pas exposés |
| la sonie intégrée (LUFS) et la crête vraie | `mix.loudness` | la cible d'un master se donne en LUFS |
| le spectre de chaque piste et du master | `mix.spectrum` | équilibrer graves et aigus, choisir où égaliser |
| repérer les pistes qui se masquent | `mix.masking` | deux pistes dans la même bande |
| la corrélation et la largeur stéréo | `mix.stereo` | compatibilité mono, largeur |
| la dynamique : facteur de crête, réduction de gain | `mix.dynamics` | savoir si une piste est trop ou pas assez compressée |
| automatiser un réglage dans le temps | `automation.write` | hors périmètre |
| déclencher un compresseur par une autre piste | `track.set_sidechain` | le kick qui fait respirer la basse |
| comparer à un morceau de référence | `mix.reference` | juger un mix contre un autre |

**Mon ordre si c'était à refaire demain :**
1. `mix.measure` : le moteur sait déjà le faire (`renderAsPlayed` et les totaux des prises).
2. `plugin.parameters` : sans lui, un effet inséré est une boîte noire pour le copilote.
3. Un EQ et un compresseur internes, dont les paramètres ont un sens fixe.

Les autres mesures viennent après.

## 8. Le presse-papiers du piano-roll et du rack

**Un seul presse-papiers pour les deux.** `ui::Clipboard` est partagé : des notes copiées dans l'un se collent
dans l'autre. Le calcul du coller est une fonction pure, `planPaste`, testée dans la suite domaine.

| Geste | Piano-roll | Rack |
|---|---|---|
| Ctrl + clic | prend ou rend une note | prend ou rend un canal |
| Ctrl + glisser dans le vide | prend une zone de notes | — |
| Ctrl+C | les notes prises | les lignes entières des canaux pris, dans le pattern affiché |
| Ctrl+V | à la tête de lecture, sur la grille ; à la place d'origine si le pattern ne sonne pas | pareil ; la ligne i va sur le i-ème canal pris, sinon sur son canal d'origine |
| Ctrl+B | juste après la copie, arrondi à la mesure ; les copies deviennent la sélection | pareil |
| Suppr | retire les notes prises, en une entrée | — |

**Les trois règles validées :**
1. Un coller de N notes, ligne ouverte comprise, est **une** entrée d'historique.
2. Une note qui tomberait au-delà du pattern n'est pas collée par Ctrl+V ; Ctrl+B, lui, **allonge le pattern**
   à la mesure dans le même groupe.
3. Jamais deux notes de même hauteur au même temps.

**Écart dit :** la playlist garde son propre presse-papiers d'arrangement, qui n'a pas été déplacé dans l'objet
partagé comme annoncé. Il fonctionnait, et le déplacer ne changeait rien pour toi.

## 9. Écouter un sample dans le navigateur

Un clic sur un sample du navigateur le joue. Il sort par un second callback du périphérique audio que le moteur
a déjà ouvert ; JUCE additionne les deux, donc la chanson et l'écoute s'entendent ensemble. Rien n'entre dans
le projet. Un second clic remplace le premier sample.

Le fichier est lu à l'avance sur un thread à part, et le callback ne touche jamais le disque. **Dit :** il prend
le verrou d'`AudioTransportSource` de JUCE le temps d'un changement de source, c'est-à-dire sur un clic, jamais
en lecture continue. La contrainte « rien ne bloque » que tu as posée pour les vu-mètres, eux la tiennent
entièrement ; l'écoute ne la tient qu'à ce verrou près.

## 10. Les vérifications

| Exécution | Vérifications |
|---|---|
| `--verify` : la liste | 187, contre 98 à la S10 |
| `--verify-reopen` : fermer, rouvrir dans un autre processus | 5 |
| `--verify-legacy` : un projet S8 + S9 | 4 |
| `--verify-file` : le menu Fichier jusqu'à un vrai « Enregistrer sous » | 11 |
| `--verify-reopen` sur la copie | 5 |
| **Total** | **212, aucune en échec** |

Les rapports et sept captures sont dans `docs/verification-s11/`.

**Ce qui est nouveau dans la liste**, à chaque fois dans l'ordre d'une personne et pas dans celui de l'auteur :
- **Vu-mètres :**
  - on boucle une mesure et on lit chaque piste ; on coupe la piste la plus forte en pleine lecture, puis on
    la rend ;
  - on demande la crête du master au vrai copilote. Sa réponse est retrouvée, au dixième de dB, parmi les
    lectures du vu-mètre faites pendant sa requête ;
  - au rendu, le master a la crête et l'énergie du fichier.
- **Playlist :**
  - défiler pour poser à la mesure 100 ;
  - zoomer autour du pointeur, dézoomer trop loin ;
  - jouer zoomé : la page tourne ;
  - Ctrl+Z au bout de la chanson.
- **Aperçus et formes d'onde :** reconstructions comptées, gel mesuré, dix dépôts pour une mesure.
- **Presse-papiers :**
  - Ctrl + clic pris, rendu, repris ;
  - copie dans le piano-roll, collage dans un autre pattern, puis dans le rack ;
  - quatre Ctrl+B jusqu'à une mesure de plus ;
  - lignes du rack copiées vers un autre pattern ;
  - tout défait à l'octet près.
- **Mixer :**
  - F10, puis « + Bus » ;
  - le Kick envoyé dans le bus, le fader du bus glissé à la main : le rendu baisse du niveau du fader, à 0,1 dB
    près ;
  - S sur le Hat pendant la lecture : le vu-mètre du Kick tombe ;
  - le rapport, sans modèle ;
  - tout défait à l'octet près.
- **Écoute :**
  - un clic sur un sample : la sortie de l'écoute mesure -1,9 dBFS, et le projet ne bouge pas d'un octet ;
  - le Clap remplace le Kick ;
  - l'écoute se superpose à la chanson en lecture.

**Ce que les vérifications ont trouvé, en plus du §3 :**
- La première version des étapes des vu-mètres attendait un son continu. Or le pattern n'a ses kicks que dans
  la première de ses quatre mesures : les vu-mètres affichaient -100 pendant 8 s, **et ils avaient raison**.
  L'erreur était dans la vérification. Elle boucle maintenant une mesure pleine.
- Les dossiers de drumkit ajoutés au navigateur par les vérifications restaient dans les réglages de la
  machine, ceux de ta session S10 compris. La liste les retire maintenant en fin de passage, et j'ai vidé à
  la main les cinq qui traînaient. Une copie des réglages est gardée à côté : `DAW IA.layout.bak-s11`.
- Le coller sur le Hat a refusé des doublons là où je ne les attendais pas. Le refus était juste ; c'était le
  test qui se trompait.
- Mes premiers comptes d'aperçus lisaient le compteur avant la notification asynchrone du projet : ils
  passaient sans rien prouver. Ils lisent maintenant une étape plus tard.
- La première mesure de gel incluait la capture PNG que prend la vérification elle-même. Elle s'arrête
  maintenant au moment où la forme d'onde revient.

**Un échec intermittent, dit :** un passage a vu « la barre de DAW IA est affichée » en échec juste après les
étapes du mixer. La capture du même instant montre la barre, et le passage suivant l'a passée sans rien changer.
Je ne l'ai pas expliqué.

## 11. La CI et check-all

`scripts/check-all.sh` a tourné avant chaque commit, sans exception. Il a arrêté deux fois un défaut avant le
push :
- un littéral d'espacement dans `MixerPanel` ;
- une ligne trop longue pour ruff.

**Écart dit :** la page Mixer et le presse-papiers sont dans le même commit (`e3a8b2d`). Ils partagent quatre
fichiers, et je n'ai pas découpé le diff : c'est le seul commit non atomique de la semaine.

## 12. Ce qui n'est pas fait, ou pas jusqu'au bout

| Point | Où en est-il |
|---|---|
| Un seul envoi visible par tranche dans le Mixer | le domaine en tient autant qu'il y a de bus ; le copilote les atteint tous |
| Les inserts d'un bus ou du master ne se posent pas depuis l'écran | la page Plugins (F8) suit la piste sélectionnée, et un bus n'en est pas une. Le copilote peut le faire |
| Les clips audio ne passent pas par les inserts de leur piste | ils sont sur la piste compagnon, qui reçoit volume, pan et routage, pas les effets. Un bus contourne le problème |
| Envois pré-fader | un champ à ajouter, absent = post |
| Réordonner les bus | `track.reorder` ne connaît que les canaux |
| Balance des bus en pan extrême | +6 dB d'un côté (§5) |
| Arrêt possible de la lecture dans les 2 premières secondes après le lancement | vu dans le harnais seulement (§3) |

**IDEES.md :** rien n'y a été mis en œuvre, et rien ne m'a semblé utile à cette semaine.

## 13. Ce que tu vérifies toi-même, dans l'ordre

Lance l'app sur un projet, en beatmaker. Pour la liste des vérifications :

```bash
"build/windows-msvc/core/app/daw_app_artefacts/Debug/DAW IA.exe" --project verif.dawproj --workspace beatmaker --verify verif
```

1. **À l'oreille, le volume général.** Le master est maintenant à 0 dB au lieu de -3 dB : un projet d'avant
   sonne 3 dB plus fort. C'est voulu, mais écoute-le avant de toucher à un fader.
2. **À l'œil, la playlist longue.** Pose un pattern à une trentaine de mesures, à la molette avec Maj.
   - Ctrl + molette doit zoomer sans que la mesure sous le pointeur bouge.
   - Sous environ 24 px de bloc, l'aperçu doit disparaître.
   - En SONG, lance la lecture zoomé : la page doit tourner.
3. **À l'œil, les aperçus.** Allume une case du rack : **tous** les blocs de ce pattern changent en même temps,
   les autres non.
4. **À l'œil puis à l'oreille, un long sample.** Dépose un morceau de plusieurs minutes. Le bloc doit
   apparaître vide, puis se remplir, sans que l'écran gèle.
5. **À l'oreille, l'écoute du navigateur.** Clique un kick, puis un autre : le second doit remplacer le
   premier. Recommence pendant que la chanson joue.
6. **Au Mixer (F10), à l'oreille :**
   - crée un bus, envoie-y deux pistes, baisse le bus : les deux baissent ensemble ;
   - mets une piste en solo : le reste se tait, et les tranches qui ne s'entendent plus se grisent ;
   - fais crier une piste au-delà de 0 dB : le voyant rouge doit s'allumer, et un clic sur le vu-mètre
     l'éteint.
7. **Au Mixer, une réverbe partagée.** Il faut un plugin de réverbe installé et le copilote : demande-lui
   « crée un bus Réverbe avec une réverbe et envoie la caisse claire dedans à -12 dB ». C'est la première
   demande de mixage réelle. Je ne l'ai pas faite, faute de réverbe connue sur la machine de test.
8. **« Mixer par l'IA »** : lis la liste. Si l'ordre du §7 ne te va pas, c'est lui qui guidera la suite.
9. **Au piano-roll et au rack :**
   - Ctrl + clic sur trois notes, Ctrl+C ;
   - change de pattern, Ctrl+V ;
   - puis Ctrl+B plusieurs fois au bout du pattern : il doit s'allonger d'une mesure ;
   - chaque geste doit s'annuler d'un seul Ctrl+Z.

## 14. Ce que la S12 hérite

| Pièce | Usage |
|---|---|
| `MeterTapPlugin`, `LevelMeters::totals()` | la base de `mix.measure` : les totaux d'un rendu existent déjà |
| `engine::renderAsPlayed` | tout rendu qui doit sonner comme on joue |
| `mix.levels` | le copilote lit ce qui sonne |
| `ProjectState::isAudible`, `findStrip`, bus et master | le mixage de la main et du copilote |
| `MixingReadiness` | la liste de ce qui manque, épinglée par un test |
| `ui::Clipboard`, `planPaste` | tout coller de notes, y compris d'une génération |
| `PatternPreviews`, `WaveformPeaks` | dessiner du contenu sans le recalculer |

## 15. Suivi d'avancement

| S | Visait | État |
|---|---|---|
| S1–S10 | Socle, bus, moteur, plugins, persistance, UI, domaine, copilote, rack, playlist | Acquis |
| S11 | Ergonomie, playlist lisible, mixage et ses mesures | Livré ; 212 vérifications ; restent tes écoutes du §13 |

Aucune décision d'architecture n'a été rouverte. La règle des identifiants tient : bus, envois collés, notes
collées, tout identifiant est tiré par l'appelant, jamais par une commande.
