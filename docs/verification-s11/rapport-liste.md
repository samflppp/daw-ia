# Vérification S11 — 26 Sep 2026 8:04:06am


## 1. disposition
- OK : le transport est dans la barre
- OK : la page playlist est ouverte
- OK : la page channel_rack est ouverte
- OK : la page history est ouverte
- OK : la page copilot est ouverte
- OK : le piano-roll est fermé
- OK : PAT est allumé
- capture : `1-disposition.png`

## 2. F7 ouvre le piano-roll, F7 le referme
- OK : F7 ouvre le piano-roll
- OK : F7 referme le piano-roll, qui était devant
- capture : `2-F7-ouvre-le-piano-roll-F7-le-referme.png`

## 3. deux pistes, un pattern qui ne se pose pas
- capture : `3-deux-pistes-un-pattern-qui-ne-se-pose-pas.png`

## 4. le pattern existe, sans placement
- OK : un pattern
- OK : aucune pose dans la playlist
- OK : une entrée d'historique
- capture : `4-le-pattern-existe-sans-placement.png`

## 5. le mode pattern joue le pattern sans pose
- écoute : `3-mode-pattern.wav`, 8.00 s, 4 attaques
- OK : le rendu dure la longueur du pattern, 8 s (16 temps)
- OK : quatre kicks, sur les pas 1, 5, 9, 13
- capture : `5-le-mode-pattern-joue-le-pattern-sans-pose.png`

## 6. poser le pattern huit fois
- OK : huit poses
- OK : aux mesures 1, 5, 9 … 29
- OK : SONG est pris
- écoute : `4-chanson-huit-poses.wav`, 64.00 s, 32 attaques
- OK : le morceau dure 32 mesures, 64 s
- OK : 32 kicks : quatre par pose
- capture : `6-poser-le-pattern-huit-fois.png`

## 7. en chanson, la lecture traverse la playlist
- capture : `7-en-chanson-la-lecture-traverse-la-playlist.png`

## 8. LA preuve : une édition du rack, huit poses changées
- OK : la ligne du Hat est ouverte dans le pattern
- OK : toujours huit poses, aucune copiée
- écoute : `5-preuve-hat-partout.wav`, 64.00 s, 64 attaques
- OK : 64 attaques : le hat s'entend dans les huit poses
- OK : aux mêmes pas dans chacune des huit poses
- capture : `8-LA-preuve--une-édition-du-rack-huit-poses-changées.png`

## 9. Ctrl+Z retire le hat des huit poses
- quatre clics séparés = 4 entrées d'historique ; autant de Ctrl+Z
- écoute : `5b-apres-ctrl-z.wav`, 64.00 s, 32 attaques
- OK : de retour à 32 attaques, dans les huit poses à la fois
- capture : `9-Ctrl+Z-retire-le-hat-des-huit-poses.png`

## 10. glisser un bloc, en retirer un
- OK : le 4e bloc a suivi, à la mesure près (mesure 15)
- OK : un glissé = une seule entrée d'historique
- OK : les sept autres n'ont pas bougé
- OK : clic droit : le bloc disparaît
- OK : le pattern reste
- capture : `10-glisser-un-bloc-en-retirer-un.png`

## 11. deux Ctrl+Z remettent tout en place
- OK : l'arrangement est identique, à l'octet près
- capture : `11-deux-Ctrl+Z-remettent-tout-en-place.png`

## 12. un deuxième pattern pour le copilote
- OK : deux patterns
- OK : le pattern 2 n'est pas posé
- capture : `12-un-deuxième-pattern-pour-le-copilote.png`

## 13. le copilote est prêt
- capture : `13-le-copilote-est-prêt.png`

## 14. « répète le pattern 1 huit fois puis ajoute le pattern 2 »
- OK : le copilote répond
- capture : `14-«-répète-le-pattern-1-huit-fois-puis-ajoute-le-pattern-2-».png`

## 15. ce que le copilote a fait
- réponse : « Le pattern 1 était déjà répété huit fois (temps 0 à 128) ; j'ai ajouté le pattern 2 à la suite, à partir de la mesure 33. »
- OK : une seule entrée d'historique
- OK : marquée copilote
- poses du pattern 1 : 8, du pattern 2 : 1
- OK : le pattern 1 est posé au moins huit fois
- OK : le pattern 2 est posé une fois
- OK : le pattern 2 vient après le pattern 1
- OK : aucun pattern créé pour répéter
- capture : `15-ce-que-le-copilote-a-fait.png`

## 16. un Ctrl+Z défait tout ce que le copilote a fait
- OK : l'arrangement d'avant la demande, à l'octet près
- OK : Ctrl+Y le refait
- capture : `16-un-Ctrl+Z-défait-tout-ce-que-le-copilote-a-fait.png`

## 17. PAT isole le pattern 2
- OK : PAT est pris
- OK : sur le pattern 2
- écoute : `8-pat-pattern-2.wav`, 8.00 s, 8 attaques
- OK : le rendu ne dure que le pattern, 8 s
- OK : seuls les huit hats du pattern 2
- OK : SONG ramène l'arrangement
- capture : `17-PAT-isole-le-pattern-2.png`

## 18. le tempo à 90
- écoute : `9a-tempo-120.wav`, 72.00 s, 40 attaques
- écoute : `9b-tempo-90.wav`, 96.00 s, 40 attaques
- OK : le morceau s'allonge de 4/3 : 72.000000 s -> 96.000000 s
- OK : chaque attaque reste sur son pas : aucun décalage entre les pistes
- capture : `18-le-tempo-à-90.png`

## 19. renommer
- le renommage passe par le bus : la boîte de dialogue de la playlist est modale, et une vérification automatique ne tape pas dedans
- capture : `19-renommer.png`

## 20. le nouveau nom dans le rack, puis supprimer et rétablir
- OK : « Refrain » dans le sélecteur du rack
- OK : supprimé, avec ses poses
- OK : Ctrl+Z le remet au même endroit
- capture : `20-le-nouveau-nom-dans-le-rack-puis-supprimer-et-rétablir.png`

## 21. Espace lance la lecture
- capture : `21-Espace-lance-la-lecture.png`

## 22. Espace l'arrête
- OK : la lecture tournait
- capture : `22-Espace-l'arrête.png`

## 23. le navigateur montre un drumkit
- OK : la page Navigateur est ouverte
- OK : le dossier du drumkit est dans l'arbre
- OK : ses deux samples y sont
- OK : un sample se glisse par son chemin
- capture : `23-le-navigateur-montre-un-drumkit.png`

## 24. déposer un sample sous les canaux du rack crée un canal sampler
- OK : le rack accepte un dépôt
- OK : un sample du navigateur l'intéresse
- OK : un canal de plus
- OK : une seule entrée d'historique
- OK : le canal joue « Kick 808.wav »
- OK : nommé d'après le sample
- octets copiés dans le projet : 35384, empreinte d8e7a4e21db5…
- capture : `24-déposer-un-sample-sous-les-canaux-du-rack-crée-un-canal-sampler.png`

## 25. le canal sampler joue ses cases
- écoute : `20a-avant-sampler.wav`, 10.67 s, 8 attaques
- écoute : `20b-canal-sampler.wav`, 10.67 s, 8 attaques
- OK : le sample s'entend sur les pas 2, 6, 10, 14 qu'on vient d'allumer
- OK : les hats du pattern sont toujours là : le sampler s'ajoute, il ne remplace rien
- attaques avant : 8, après : 8 ; un hat collé derrière un coup du sampler est sous sa queue
- capture : `25-le-canal-sampler-joue-ses-cases.png`

## 26. déposer un sample sur la playlist pose un clip audio
- OK : la playlist accepte un dépôt
- OK : un clip audio
- OK : une seule entrée d'historique : la piste et le clip ensemble
- OK : le dépôt passe en mode chanson : en PAT, le clip serait muet
- OK : posé à la mesure sous le pointeur, temps 148.000000
- OK : il dure son sample : 1,5 s
- écoute : `21-clip-audio.wav`, 100.17 s, 41 attaques
- OK : le clap s'entend à son temps
- OK : le rendu va jusqu'au bout du clap
- capture : `26-déposer-un-sample-sur-la-playlist-pose-un-clip-audio.png`

## 27. Ctrl + glisser sélectionne une zone
- OK : deux blocs pris dans la zone : 2
- capture : `27-Ctrl-+-glisser-sélectionne-une-zone.png`

## 28. Ctrl + clic ajoute un bloc à la sélection, Ctrl + Maj + clic aussi
- OK : Ctrl + clic : trois blocs sélectionnés
- OK : un second Ctrl + clic le retire
- OK : Ctrl + Maj + clic, le geste de la S10, l'ajoute aussi
- OK : et le retire
- capture : `28-Ctrl-+-clic-ajoute-un-bloc-à-la-sélection-Ctrl-+-Maj-+-clic-aussi.png`

## 29. Ctrl+B duplique la sélection juste après elle
- OK : deux poses de plus
- OK : une seule entrée d'historique
- OK : les copies commencent aux temps 32 et 48
- OK : la sélection passe aux copies, prête pour un autre Ctrl+B
- OK : un Ctrl+Z les retire toutes les deux
- capture : `29-Ctrl+B-duplique-la-sélection-juste-après-elle.png`

## 30. Ctrl+C puis Ctrl+V colle à la tête de lecture
- OK : un bloc sélectionné au clic
- OK : une pose de plus
- OK : au temps 100, là où est la tête
- capture : `30-Ctrl+C-puis-Ctrl+V-colle-à-la-tête-de-lecture.png`

## 31. Suppr retire la sélection, Ctrl+Z la rend
- OK : le bloc collé est retiré
- OK : Ctrl+Z le remet, à l'octet près
- capture : `31-Suppr-retire-la-sélection-Ctrl+Z-la-rend.png`

## 32. un clic sur « Kick 808.wav » dans le navigateur le fait entendre
- OK : le sample est en écoute
- capture : `32-un-clic-sur-«-Kick-808.wav-»-dans-le-navigateur-le-fait-entendre.png`

## 33. le projet n'a pas bougé ; un clic sur « Clap.wav » remplace le premier
- OK : la sortie de l'écoute mesure -1.9 dBFS
- OK : aucune entrée d'historique
- OK : le projet est identique, à l'octet près
- OK : le Clap remplace le Kick
- capture : `33-le-projet-n'a-pas-bougé--un-clic-sur-«-Clap.wav-»-remplace-le-premier.png`

## 34. pendant que la chanson joue, l'écoute s'y ajoute
- capture : `34-pendant-que-la-chanson-joue-l'écoute-s'y-ajoute.png`

## 35. arrêter : la lecture et l'écoute
- OK : la chanson et l'écoute sonnent ensemble
- OK : plus rien en écoute
- OK : toujours aucune entrée d'historique
- capture : `35-arrêter--la-lecture-et-l'écoute.png`

## 36. un morceau de cent mesures : on défile pour poser au bout
- OK : la mesure 100 est amenée à l'écran à la molette
- OK : une pose de plus
- OK : à la mesure 100
- largeur d'un temps : 6.0 px, premier temps visible : 322.4
- OK : un temps n'est jamais plus étroit que 6 px : 24 px par mesure au moins
- capture : `36-un-morceau-de-cent-mesures--on-défile-pour-poser-au-bout.png`

## 37. Ctrl + molette zoome autour du pointeur
- zoom : 6.0 -> 12.0 px par temps
- OK : quatre crans doublent la largeur
- OK : le temps sous le pointeur y reste, au pixel près
- capture : `37-Ctrl-+-molette-zoome-autour-du-pointeur.png`

## 38. dézoomer trop loin s'arrête à la largeur lisible
- OK : soixante crans en arrière : 6.0 px par temps, le plancher
- capture : `38-dézoomer-trop-loin-s'arrête-à-la-largeur-lisible.png`

## 39. zoomé au début, Espace en SONG : la vue suit la tête de lecture
- OK : la vue est au début
- capture : `39-zoomé-au-début-Espace-en-SONG--la-vue-suit-la-tête-de-lecture.png`

## 40. la tête de lecture reste dans la vue
- tête au temps 11.02, vue de 8.0 à 17.8
- OK : la page a tourné, la tête est à l'écran
- OK : la page commence sur une mesure
- capture : `40-la-tête-de-lecture-reste-dans-la-vue.png`

## 41. au bout de la chanson, Ctrl+Z retire la pose lointaine ; la molette ramène au début
- OK : la vue est au bout, sur la pose de la mesure 100
- OK : la pose de la mesure 100 est retirée
- OK : la vue reste où elle était
- OK : la molette ramène au début de la chanson
- OK : la vue montre de nouveau le début
- capture : `41-au-bout-de-la-chanson-Ctrl+Z-retire-la-pose-lointaine--la-molette-ramène-au-début.png`

## 42. déplacer un bloc
- aperçus construits jusqu'ici : 6
- capture : `42-déplacer-un-bloc.png`

## 43. aucun aperçu reconstruit ; Ctrl+Z
- OK : un bloc déplacé : aucun aperçu reconstruit
- capture : `43-aucun-aperçu-reconstruit--Ctrl+Z.png`

## 44. toujours aucun ; allumer une case du pattern 1 dans le rack
- OK : le Ctrl+Z du déplacement non plus
- OK : la case est allumée : une entrée d'historique
- capture : `44-toujours-aucun--allumer-une-case-du-pattern-1-dans-le-rack.png`

## 45. un aperçu reconstruit, pour les huit poses ; Ctrl+Z
- aperçus construits : 7
- OK : une note ajoutée au pattern 1 : exactement un aperçu reconstruit, pour ses huit poses
- capture : `45-un-aperçu-reconstruit-pour-les-huit-poses--Ctrl+Z.png`

## 46. le Ctrl+Z reconstruit l'aperçu une fois
- OK : une construction de plus, pas davantage
- capture : `46-le-Ctrl+Z-reconstruit-l'aperçu-une-fois.png`

## 47. mesurer un sample de deux minutes : le thread message continue
- copie de 21 Mo dans le projet : 170.9 ms
- OK : la première demande rend la main tout de suite, sans forme d'onde
- durée de la demande : 0.1 ms
- capture : `47-mesurer-un-sample-de-deux-minutes--le-thread-message-continue.png`

## 48. aucun gel pendant la mesure ; déposer un autre long sample sur la playlist
- mesure faite en 1385.35 ms ; plus long écart entre deux tics : 124.246 ms (un tic toutes les 120 ms)
- OK : aucun gel : jamais plus de 400 ms sans tic
- OK : une mesure
- dépôt, copie et pose du clip par le moteur comprises : 461.473 ms
- OK : un dépôt, une entrée d'historique
- OK : juste après le dépôt, la forme d'onde est en cours de mesure : le bloc est vide
- capture : `48-aucun-gel-pendant-la-mesure--déposer-un-autre-long-sample-sur-la-playlist.png`

## 49. le bloc s'est rempli
- après le dépôt, plus long écart entre deux tics : 278.945 ms — le moteur reconstruit son graphe pour le nouveau clip sur le thread message, comme à chaque dépôt depuis la S10 ; la mesure seule est à l'étape d'avant
- OK : une mesure pour ce sample
- OK : la forme d'onde couvre 121 s
- capture : `49-le-bloc-s'est-rempli.png`

## 50. neuf dépôts de plus du même sample
- capture : `50-neuf-dépôts-de-plus-du-même-sample.png`

## 51. dix clips, une seule mesure ; dix Ctrl+Z
- OK : toujours une seule mesure pour dix clips du même sample (1)
- OK : les dix dépôts défaits
- OK : il reste le clap
- capture : `51-dix-clips-une-seule-mesure--dix-Ctrl+Z.png`

## 52. dans le piano-roll, Ctrl + clic prend deux kicks, puis en rend un, puis le reprend
- OK : le piano-roll montre les kicks
- OK : deux notes prises
- OK : un second Ctrl + clic en rend une
- OK : et un troisième la reprend
- OK : copier n'écrit rien
- capture : `52-dans-le-piano-roll-Ctrl-+-clic-prend-deux-kicks-puis-en-rend-un-puis-le-reprend.png`

## 53. autre pattern, Ctrl+V : les deux kicks à la tête de lecture, une entrée
- OK : deux notes dans le pattern 2, sur la ligne du Kick ouverte au besoin
- OK : un seul Ctrl+V, une seule entrée d'historique
- collées aux temps 0.00 et 2.00 : la tête est au temps 0, l'écart d'origine est gardé
- capture : `53-autre-pattern-Ctrl+V--les-deux-kicks-à-la-tête-de-lecture-une-entrée.png`

## 54. Ctrl+B quatre fois : les copies s'enchaînent, le pattern s'allonge à la cinquième mesure
- longueur du pattern 2 : 16 -> 20 temps
- OK : quatre Ctrl+B, quatre entrées
- OK : le dernier Ctrl+B a allongé le pattern pour tenir sa copie
- OK : à la mesure
- OK : la sélection suit les copies
- OK : quatre Ctrl+Z : la longueur revient
- capture : `54-Ctrl+B-quatre-fois--les-copies-s'enchaînent-le-pattern-s'allonge-à-la-cinquième-mesure.png`

## 55. ce que le piano-roll a copié, collé dans le rack sur le Hat
- OK : le Hat est pris
- notes du Hat dans le pattern 2 : 8 -> 10 (une note déjà là à la même place et à la même hauteur n'est pas doublée)
- OK : des notes arrivent sur le Hat
- OK : une entrée
- capture : `55-ce-que-le-piano-roll-a-copié-collé-dans-le-rack-sur-le-Hat.png`

## 56. dans le rack, Kick et Hat pris au Ctrl + clic, copiés, collés dans le pattern 1
- OK : deux canaux pris
- OK : le collage de deux lignes est une entrée
- historique : « coller 10 notes », 11 commandes
- capture : `56-dans-le-rack-Kick-et-Hat-pris-au-Ctrl-+-clic-copiés-collés-dans-le-pattern-1.png`

## 57. tout défaire : le projet d'avant le presse-papiers, à l'octet près
- OK : à l'octet près
- capture : `57-tout-défaire--le-projet-d'avant-le-presse-papiers-à-l'octet-près.png`

## 58. boucler la première mesure, Espace : les vu-mètres bougent
- capture : `58-boucler-la-première-mesure-Espace--les-vu-mètres-bougent.png`

## 59. chaque piste mesure ce qu'elle joue, et seulement ça
- master : crête -11.1 dBFS, RMS -19.7 dBFS
- OK : le master mesure la lecture
- Kick : crête -11.1 dBFS
- Hat : crête -100.0 dBFS
- Kick 808 : crête -100.0 dBFS
- Clap : crête -100.0 dBFS
- OK : au moins une piste de la mesure bouclée mesure un signal
- OK : la piste du clap, hors de la boucle, reste à -100 dBFS
- OK : le master n'est jamais sous sa piste la plus forte
- capture : `59-chaque-piste-mesure-ce-qu'elle-joue-et-seulement-ça.png`

## 60. couper la piste la plus forte pendant la lecture
- capture : `60-couper-la-piste-la-plus-forte-pendant-la-lecture.png`

## 61. la rendre : son vu-mètre repart au coup suivant
- OK : coupée, elle mesure -100 dBFS en moins de 2 s
- capture : `61-la-rendre--son-vu-mètre-repart-au-coup-suivant.png`

## 62. le copilote lit les vu-mètres
- capture : `62-le-copilote-lit-les-vu-mètres.png`

## 63. ce qu'il a lu est ce que le vu-mètre mesure
- réponse : « Le niveau crête du master est de -100 dBFS (aucun signal audible sur les 300 dernières ms, comme tout est silencieux actuellement). »
- 41 lectures du vu-mètre du master pendant la demande
- OK : aucune entrée d'historique : une lecture ne modifie rien
- OK : le master sonnait pendant la demande
- OK : la réponse donne, au dixième de dB, une crête que le vu-mètre du master a lue pendant la demande (entre -100.0 et -11.1 dBFS)
- capture : `63-ce-qu'il-a-lu-est-ce-que-le-vu-mètre-mesure.png`

## 64. Espace arrête : tout retombe à -100
- OK : aucun bloc perdu pendant la lecture en direct
- capture : `64-Espace-arrête--tout-retombe-à--100.png`

## 65. au rendu, le master mesure ce que le fichier contient
- fichier : 100.17 s, crête -4.95 dBFS ; vu-mètre du master : 101.15 s, crête -4.95 dBFS
- OK : même crête, à 0,1 dB près
- OK : même énergie, à 0,1 dB près
- OK : en SONG, la piste du clap mesure son clip : sa piste compagnon a sa prise
- capture : `65-au-rendu-le-master-mesure-ce-que-le-fichier-contient.png`

## 66. F10 ouvre le mixer : une tranche par piste, puis le master
- capture : `66-F10-ouvre-le-mixer--une-tranche-par-piste-puis-le-master.png`

## 67. la page Mixer est ouverte
- OK : la page Mixer est à l'écran
- OK : 5 tranches : 4 pistes, 0 bus, le master
- capture : `67-la-page-Mixer-est-ouverte.png`

## 68. « + Bus » crée un bus, une entrée d'historique
- OK : un bus
- OK : une entrée d'historique
- capture : `68-«-+-Bus-»-crée-un-bus-une-entrée-d'historique.png`

## 69. envoyer le Kick dans le bus, baisser le bus : le rendu baisse de ce qu'il faut
- OK : la tranche du Kick a une sortie
- OK : le Kick sort dans le bus
- OK : un glissé du fader, une entrée d'historique
- Kick direct : -11.10 dBFS ; par le bus à -25.27 dB : -36.38 dBFS
- OK : le fader du bus est descendu
- OK : le rendu baisse du niveau du fader du bus, à 0,1 dB près
- capture : `69-envoyer-le-Kick-dans-le-bus-baisser-le-bus--le-rendu-baisse-de-ce-qu'il-faut.png`

## 70. en lecture, S sur le Hat : seul le Hat s'entend
- OK : le Hat est en solo, dans le projet
- OK : le Kick n'est plus entendu
- capture : `70-en-lecture-S-sur-le-Hat--seul-le-Hat-s'entend.png`

## 71. le vu-mètre du Kick est tombé ; S de nouveau, il repart
- OK : le Kick mesure -100 dBFS pendant le solo du Hat
- capture : `71-le-vu-mètre-du-Kick-est-tombé--S-de-nouveau-il-repart.png`

## 72. « Mixer par l'IA » : aucun modèle appelé, la liste de ce qui manque
- rapport :

```
Aucun modèle appelé : ceci vérifie ce que le copilote peut atteindre pour mixer.
14 besoins sur 25 sont couverts. Ce qui manque encore :
• connaître les paramètres d'un effet : noms, unités, plages, valeurs actuelles (plugin.parameters) — l'état ne liste que les paramètres déjà touchés, par identifiant et en valeur normalisée : une IA ne sait pas que « 0,42 » est une fréquence de coupure à 800 Hz
• disposer d'effets de base connus quelle que soit la machine : EQ, compresseur, limiteur (mix.stock_effects) — les plugins installés varient d'une machine à l'autre et leurs paramètres n'ont pas de sens commun
• mesurer crête et RMS sur un passage choisi, par un rendu (mix.measure) — 300 ms en direct ne disent rien d'un refrain ; les totaux d'un rendu existent dans le moteur mais ne sont pas exposés au copilote
• mesurer la sonie intégrée (LUFS) et la crête vraie (mix.loudness) — la cible d'un master se donne en LUFS, pas en RMS
• mesurer le spectre de chaque piste et du master (mix.spectrum) — équilibrer les graves et les aigus, choisir où égaliser
• repérer les pistes qui se masquent (mix.masking) — deux pistes dans la même bande de fréquences
• mesurer la corrélation et la largeur stéréo (mix.stereo) — la compatibilité mono, la largeur
• mesurer la dynamique : facteur de crête, réduction de gain d'un compresseur (mix.dynamics) — savoir si une piste est trop compressée ou pas assez
• automatiser un réglage dans le temps (automation.write) — hors périmètre de la S11
• déclencher un compresseur par une autre piste (sidechain) (track.set_sidechain) — le kick qui fait respirer la basse
• comparer à un morceau de référence (mix.reference) — juger un mix contre un autre
```
- OK : le copilote n'a rien reçu
- OK : le rapport prend la place des tranches
- OK : un compte des besoins couverts
- OK : les manques y sont, nommés : mesure sur un passage, paramètres des effets
- capture : `72-«-Mixer-par-l'IA-»--aucun-modèle-appelé-la-liste-de-ce-qui-manque.png`

## 73. tout défaire au Ctrl+Z, dans l'ordre inverse
- OK : le même bouton rend les tranches
- OK : l'historique est revenu à son point de départ
- OK : le projet d'avant le mixer, à l'octet près
- capture : `73-tout-défaire-au-Ctrl+Z-dans-l'ordre-inverse.png`

## 74. la barre de titre remplace celle du système
- OK : plus de barre de titre Windows
- OK : la barre de DAW IA est affichée
- OK : elle porte le nom du projet : « verif »
- OK : le menu Fichier
- OK : le bouton Beatmaker, allumé
- OK : Fichier, quatre workspaces, réduire, agrandir, fermer
- OK : le transport ne porte plus les workspaces
- capture : `74-la-barre-de-titre-remplace-celle-du-système.png`

## 75. Ctrl+S enregistre
- OK : la barre dit « enregistré »
- capture : `75-Ctrl+S-enregistre.png`

## 76. double-clic sur la barre : agrandir, puis rendre sa taille
- OK : la fenêtre est agrandie
- capture : `76-double-clic-sur-la-barre--agrandir-puis-rendre-sa-taille.png`

## 77. le second double-clic rend la taille d'avant
- OK : elle ne l'est plus
- OK : et elle a repris sa place
- capture : `77-le-second-double-clic-rend-la-taille-d'avant.png`

## 78. les workspaces se changent depuis la barre
- OK : les deux boutons existent
- capture : `78-les-workspaces-se-changent-depuis-la-barre.png`

## 79. Découverte est affiché, puis retour au beatmaker
- OK : Découverte est allumé
- capture : `79-Découverte-est-affiché-puis-retour-au-beatmaker.png`

## Résultat
- 187 vérifications passées, 0 en échec