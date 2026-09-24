# Vérification S10 — 24 Sep 2026 9:36:01pm


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
- réponse : « Le pattern 1 était déjà répété huit fois (0 à 128). J'ai ajouté le pattern 2 à la suite, à partir de la mesure 128. »
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

## Résultat
- 51 vérifications passées, 0 en échec