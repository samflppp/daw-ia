# Vérification S11 — 26 Sep 2026 1:08:23am


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
- réponse : « Le pattern 1 était déjà posé huit fois d'affilée (mesures 1 à 33) ; j'ai ajouté le pattern 2 à la suite, à partir de la mesure 33. »
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

## 32. un morceau de cent mesures : on défile pour poser au bout
- OK : la mesure 100 est amenée à l'écran à la molette
- OK : une pose de plus
- OK : à la mesure 100
- largeur d'un temps : 6.0 px, premier temps visible : 322.4
- OK : un temps n'est jamais plus étroit que 6 px : 24 px par mesure au moins
- capture : `32-un-morceau-de-cent-mesures--on-défile-pour-poser-au-bout.png`

## 33. Ctrl + molette zoome autour du pointeur
- zoom : 6.0 -> 12.0 px par temps
- OK : quatre crans doublent la largeur
- OK : le temps sous le pointeur y reste, au pixel près
- capture : `33-Ctrl-+-molette-zoome-autour-du-pointeur.png`

## 34. dézoomer trop loin s'arrête à la largeur lisible
- OK : soixante crans en arrière : 6.0 px par temps, le plancher
- capture : `34-dézoomer-trop-loin-s'arrête-à-la-largeur-lisible.png`

## 35. zoomé au début, Espace en SONG : la vue suit la tête de lecture
- OK : la vue est au début
- capture : `35-zoomé-au-début-Espace-en-SONG--la-vue-suit-la-tête-de-lecture.png`

## 36. la tête de lecture reste dans la vue
- tête au temps 11.49, vue de 8.0 à 17.8
- OK : la page a tourné, la tête est à l'écran
- OK : la page commence sur une mesure
- capture : `36-la-tête-de-lecture-reste-dans-la-vue.png`

## 37. au bout de la chanson, Ctrl+Z retire la pose lointaine ; la molette ramène au début
- OK : la vue est au bout, sur la pose de la mesure 100
- OK : la pose de la mesure 100 est retirée
- OK : la vue reste où elle était
- OK : la molette ramène au début de la chanson
- OK : la vue montre de nouveau le début
- capture : `37-au-bout-de-la-chanson-Ctrl+Z-retire-la-pose-lointaine--la-molette-ramène-au-début.png`

## 38. boucler la première mesure, Espace : les vu-mètres bougent
- capture : `38-boucler-la-première-mesure-Espace--les-vu-mètres-bougent.png`

## 39. chaque piste mesure ce qu'elle joue, et seulement ça
- master : crête -11.2 dBFS, RMS -21.7 dBFS
- OK : le master mesure la lecture
- Kick : crête -11.2 dBFS
- Hat : crête -100.0 dBFS
- Kick 808 : crête -100.0 dBFS
- Clap : crête -100.0 dBFS
- OK : au moins une piste de la mesure bouclée mesure un signal
- OK : la piste du clap, hors de la boucle, reste à -100 dBFS
- OK : le master n'est jamais sous sa piste la plus forte
- capture : `39-chaque-piste-mesure-ce-qu'elle-joue-et-seulement-ça.png`

## 40. couper la piste la plus forte pendant la lecture
- capture : `40-couper-la-piste-la-plus-forte-pendant-la-lecture.png`

## 41. la rendre : son vu-mètre repart au coup suivant
- OK : coupée, elle mesure -100 dBFS en moins de 2 s
- capture : `41-la-rendre--son-vu-mètre-repart-au-coup-suivant.png`

## 42. le copilote lit les vu-mètres
- capture : `42-le-copilote-lit-les-vu-mètres.png`

## 43. ce qu'il a lu est ce que le vu-mètre mesure
- réponse : « Le niveau crête du master est actuellement de **-11,2 dBFS**. »
- 27 lectures du vu-mètre du master pendant la demande
- OK : aucune entrée d'historique : une lecture ne modifie rien
- OK : le master sonnait pendant la demande
- OK : la réponse donne, au dixième de dB, une crête que le vu-mètre du master a lue pendant la demande (entre -100.0 et -11.1 dBFS)
- capture : `43-ce-qu'il-a-lu-est-ce-que-le-vu-mètre-mesure.png`

## 44. Espace arrête : tout retombe à -100
- OK : aucun bloc perdu pendant la lecture en direct
- capture : `44-Espace-arrête--tout-retombe-à--100.png`

## 45. au rendu, le master mesure ce que le fichier contient
- fichier : 100.17 s, crête -4.95 dBFS ; vu-mètre du master : 101.43 s, crête -4.95 dBFS
- OK : même crête, à 0,1 dB près
- OK : même énergie, à 0,1 dB près
- OK : en SONG, la piste du clap mesure son clip : sa piste compagnon a sa prise
- capture : `45-au-rendu-le-master-mesure-ce-que-le-fichier-contient.png`

## 46. la barre de titre remplace celle du système
- OK : plus de barre de titre Windows
- OK : la barre de DAW IA est affichée
- OK : elle porte le nom du projet : « verif »
- OK : le menu Fichier
- OK : le bouton Beatmaker, allumé
- OK : Fichier, quatre workspaces, réduire, agrandir, fermer
- OK : le transport ne porte plus les workspaces
- capture : `46-la-barre-de-titre-remplace-celle-du-système.png`

## 47. Ctrl+S enregistre
- OK : la barre dit « enregistré »
- capture : `47-Ctrl+S-enregistre.png`

## 48. double-clic sur la barre : agrandir, puis rendre sa taille
- OK : la fenêtre est agrandie
- capture : `48-double-clic-sur-la-barre--agrandir-puis-rendre-sa-taille.png`

## 49. le second double-clic rend la taille d'avant
- OK : elle ne l'est plus
- OK : et elle a repris sa place
- capture : `49-le-second-double-clic-rend-la-taille-d'avant.png`

## 50. les workspaces se changent depuis la barre
- OK : les deux boutons existent
- capture : `50-les-workspaces-se-changent-depuis-la-barre.png`

## 51. Découverte est affiché, puis retour au beatmaker
- OK : Découverte est allumé
- capture : `51-Découverte-est-affiché-puis-retour-au-beatmaker.png`

## Résultat
- 127 vérifications passées, 0 en échec