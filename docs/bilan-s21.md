# Bilan de fin de S21 — DAW IA

**Période :** semaine 21 sur 26. Rédigé le 3 octobre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`. 8 commits S21, de `49542a8` à `2257034`, plus ce bilan. Une branche
non fusionnée, `s21-piste-compagne` (§7).
**Volume S21 sur `main` (hors ce bilan) :** 49 fichiers, +2 178 lignes, −103.
**Tests :** suite du domaine, 417 cas (+6) ; Python, 51 cas (inchangé). Moteur (label `audio`, local) : 99 cas
sur `main`, inchangés ; 101 sur la branche.
**CI :** pas encore poussé à l'écriture de ce bilan ; `./scripts/check-all.sh` vert sur chaque commit sauf un,
rattrapé (§10).
**Vérifications de l'application (Release) :** `--verify` complet **avec copilote**, 658 passées, 9 en échec,
aucune neuve (§9) ; `--verify-lecture` (neuve), 0 lecture muette sur 75 ; `--verify-mix`, 70 sur 70 ;
`--verify-canvas`, 131 sur 131 ; `scripts/verify-quit.ps1`, 31 fermetures sur 31.
**Registry :** 60 types, inchangé. **Schéma du projet :** 6, inchangé. **Aucun verbe ajouté.**

## En bref

- **La lecture muette est trouvée et corrigée.** Ce n'était pas le moteur : c'était ta carte son. Un casque
  Bluetooth qui part (tes AirPods) laisse JUCE sans périphérique, et JUCE n'en rouvre plus jamais aucun. Le
  logiciel suit désormais Windows : il rouvre une sortie dans la seconde, et revient à la tienne quand elle
  revient (§2).
- **Le premier vrai mixage par le modèle a eu lieu** : il marche, il coûte 0,074 $, et il a trouvé un bogue de
  la S20 qui comptait ses jetons à zéro (§5).
- **L'écoute avant/après était muette sur les haut-parleurs de ton portable** (huit sorties) : corrigé (§5).
- **Le kick et la basse sont jugés sur la marge du kick à ses coups**, plus sur leur recouvrement (§6).
- **Le chantier 2 de la S19 est fait** : les fenêtres se posent sur les bords et se redimensionnent ensemble ; le
  rack et l'historique défilent (§8).
- **La fermeture et les 17 étapes du copilote ne se reproduisent plus** (§3, §4).
- **Tombée : la piste compagne.** Construite et prouvée au rendu, elle rend la lecture en direct muette une
  fois sur trois, cause non trouvée. Elle reste sur une branche (§7).

## 1. Le modèle validé

Exposé en début de semaine, validé en bloc (« je valide tout, go »), puis une question en cours de route.

| Question | Réponse retenue |
|---|---|
| Ancrer une fenêtre | A : le magnétisme, comme FL. Le lien entre deux fenêtres se lit aux bords qui coïncident au début du geste, il n'est pas stocké ; le recouvrement reste possible. |
| Défiler le rack et l'historique | La grammaire S18 : molette, clic-molette, F ; aucun zoom. L'historique suit la dernière entrée, sauf si tu l'as descendu. |
| La piste compagne | Une troisième piste Tracktion par piste du domaine, toujours : la tranche, dans laquelle jouent les notes et les enregistrements. |
| Lancer le mixage réel | Une option dédiée, `--mix-once`, qui n'est pas une vérification. |
| Le critère kick / basse | `mix::hitMarginDb` dans le domaine, citable, à la place du recouvrement pour cette paire. |
| La carte son perdue (posée en cours de semaine) | **Suivre Windows** : rouvrir la sortie par défaut tout de suite, revenir sur la tienne à son retour, le dire dans l'état du transport. |

## 2. La lecture muette : la cause, et le correctif

**Reproduire.** `--verify` complet prend trop longtemps pour une panne qui tombe une fois sur trois. Une
vérification neuve, `--verify-lecture`, construit le projet de `--verify` par les mêmes étapes, puis joue sa
première mesure en boucle 75 fois. Avant chaque lecture, elle fait l'une de cinq actions, tour à tour :
- rien ;
- un rendu hors ligne de l'Edit vivant ;
- une commande ;
- l'écoute d'un sample ;
- la carte son perdue (ajoutée une fois la cause trouvée).

À chaque silence, `PlaybackProbe::describeAudio` écrit le chemin audio deux fois, à 0,5 s d'écart :
- la carte, et le temps du flux de Tracktion ;
- le contexte de lecture et son graphe ;
- chaque piste, avec ses clips et ses plugins ;
- les blocs reçus par chaque prise de niveau, grâce à un compteur neuf qui n'est jamais remis à zéro.

**La cause.** Le premier passage a reproduit le silence 57 fois sur 60. Sur 114 relevés, 113 disaient « carte
son : aucune ».
1. La sortie était tes AirPods (« Find My Stereo »). Ils sont partis pendant le passage.
2. JUCE a fermé le périphérique et n'a pas réussi à rouvrir celui qu'il retenait.
3. Ensuite, il ne regarde plus : `audioDeviceListChanged` ne fait rien tant qu'aucun périphérique n'est ouvert.
4. Le transport reste « en lecture » sans qu'un seul bloc sorte, jusqu'à la fermeture.

C'est la forme vue aux étapes 58 et 60 de la S20. La dette de la S12 (« pas d'effet pendant la lecture »)
avait deux formes, et l'autre (une action perdue) ne s'est plus montrée depuis la S13.

**Le correctif.** `AudioOutputKeeper`, dans `EngineHost` :
- quand aucune carte n'est ouverte, il rouvre dans la seconde la carte du début de session si elle est là, la
  sortie par défaut de Windows sinon ;
- quand ta carte revient, il y retourne.

Tracktion relance la lecture sur la nouvelle carte : le morceau ne s'arrête pas. Chaque changement est écrit
dans `daw.log` et sous la position du transport : « sortie perdue » en rouge tant qu'aucune carte n'est
ouverte, puis « sortie : <nom> » pendant quelques secondes.

**Preuve.**
- `--verify-lecture` : 0 lecture muette sur 75, dont 15 avec la carte perdue exprès, rouverte à chaque fois.
- Cassé une fois (gardien qui ne rouvre plus) : 71 lectures muettes sur 75, le symptôme d'origine.
- Deux `--verify` complets ensuite, les étapes 58 et 60 vertes.

**Ce que la vérification ne prouve pas :** le départ réel d'un casque Bluetooth. Elle ferme la carte comme JUCE
la ferme quand elle part ; le Bluetooth lui-même est dans ta liste d'essais (§13).

## 3. La fermeture

`scripts/verify-quit.ps1` ne cherchait la panne que sans copilote, à l'arrêt, et une seule fois. Il ferme
maintenant chaque cas 10 fois :
- sans copilote ;
- avec le copilote ;
- en lecture, avec le copilote ;
- puis une fois avec un démontage bloqué exprès.

Une fermeture que la garde de la S19 a dû finir compte comme un échec. Il donne aussi à chaque lancement sa
disposition jetable : il réécrivait la tienne.

**31 fermetures sur 31.** Les 30 normales ont pris de 265 à 368 ms, sans la garde, et la garde a tenu sur le
démontage bloqué. **La cause de la S18 n'est pas trouvée** ; elle ne s'est pas reproduite en 44 fermetures
(S19 et S21). La garde reste.

## 4. Les 17 étapes qui échouaient avec le copilote

Elles ne se reproduisent plus : deux `--verify` complets avec le copilote lancé, aucune étape de pages ou de
fenêtres en échec. **La cause n'est pas démontrée.** Le candidat le plus probable : avant `--layout` (fin de
S20), le copilote et les vérifications partageaient ton fichier de disposition, et une disposition laissée par
un passage décalait les fenêtres du suivant. Je ne l'ai pas prouvé en remettant l'ancien fonctionnement.

## 5. Le premier mixage par le modèle, et ce qu'il a trouvé

**`--mix-once`** ouvre le projet donné, attend le copilote, mixe par le modèle, écrit tout dans `daw.log`,
refuse la proposition et quitte. Ce n'est pas une vérification : il ne contrôle rien et appelle l'API. La
règle « une vérification ne mixe jamais par le modèle » tient. `daw.log` garde maintenant aussi les refus des
garde-fous, au premier tour comme au dernier.

**Sur le projet de `--verify-mix`, deux appels :**
1. Le premier a marché : décidé par `claude-sonnet-5`, quatre refus au premier tour, un réglage gardé. Mais
   les jetons valaient 0.
2. **Le bogue, de la S20 :** le service envoie `inputTokens` / `outputTokens`, et `MixSession` lisait
   `input_tokens` / `output_tokens`. Personne ne l'avait vu, faute d'appel réel. Corrigé.
3. Le second a donné deux refus au premier tour : une phrase sans son nombre, et la crête vraie du master
   citée pour le kick. Deux réglages ont été gardés au second tour, dont la basse creusée sur la marge du kick :
   « Sur ses coups, le kick ne dépasse la basse que de 1,0 dB à 63 Hz … j'ai creusé la basse de 4 dB ».

**Le coût réel :** 7 680 jetons lus et 5 822 écrits, deux tours compris, soit **0,074 $**. L'estimation de la
S20 (0,06 à 0,08 $) tient.

**Vu en route :** l'écoute avant/après de `MixComparison` sautait chaque bloc d'une carte de plus de deux
sorties. Les haut-parleurs de ton portable en déclarent huit : l'avant/après y était muet, pour toi aussi.
Les rendus vont maintenant sur les sorties 1 et 2. `--verify-mix` passe de nouveau (70 sur 70).

## 6. Le kick et la basse

- `domain::mix::hitMarginDb` mesure de combien le kick passe la basse dans l'octave qui le porte, sur le
  cinquième des instants où il est le plus fort.
- Le brief porte `kickMargins`. Le recouvrement d'un kick avec une basse en sort, donc ne peut plus être cité :
  le garde-fou refuse ce qui n'existe pas.
- Les règles creusent la basse sous 6 dB de marge, et la phrase cite `margin.<octave>.<kick>`.
- La consigne du modèle le décrit, et le modèle l'a cité au premier appel réel.

Sur les signaux de `--verify-mix`, le kick passe la basse de 1,0 dB avant le mixage et de 9,2 dB après.

## 7. Tombée : la piste compagne

**Ce qui est construit, sur la branche `s21-piste-compagne` :**
- Chaque piste du domaine devient une tranche : tes inserts après l'instrument, les effets du DAW, le fader,
  les envois, le vu-mètre.
- Dans la tranche jouent deux pistes Tracktion à l'unité : les notes (l'instrument et ce qui le précède) et
  les enregistrements.
- Tracktion les additionne avant la chaîne, comme il le fait pour un bus.
- La mesure du mixage n'a plus qu'une prise par piste, plus juste qu'une somme de deux.

**Ce qui est prouvé :**
- Un vrai effet, la Valhalla Supermassive, traite tes enregistrements : après une salve, −200 dBFS sans lui,
  −30,4 dBFS avec.
- Une seule instance dans l'Edit, sur la tranche.
- Les tests du moteur passent (100 sur 101, §11), et le test a été cassé une fois.

**Pourquoi ce n'est pas sur `main` :**
- `--verify-lecture` a trouvé une régression de la lecture en direct : 27 lectures muettes sur 75, et aucune
  après un rendu hors ligne, qui reconstruit tout le graphe.
- Les prises de niveau reçoivent leurs blocs, la piste des notes a ses clips et son 4OSC, et rien ne sort.
- Le `--verify` complet a perdu les étapes 58 et 60.

**Cause non trouvée.** Écartés : la carte son, le partage de mémoire des nœuds (déjà coupé par défaut ; essayé),
une collision d'identifiants de `SummingNode`. Faire passer ça sur `main` aurait réintroduit exactement la
panne que la semaine venait de fermer.

**Où ça va :** la dette reste ouverte, avec son modèle, ses tests et cette trace. Il faut une journée de lecture
du graphe de Tracktion (la reconstruction à chaud d'une piste qui en alimente une autre) ; c'est à toi de
décider dans quelle semaine elle tombe.

## 8. Le chantier 2 : fenêtres, rack, historique

- **Le magnétisme.** Une fenêtre glissée se pose sur le bord du bureau, ou sur celui d'une fenêtre qu'elle
  regarde, à moins de 10 px (`metric.page.snap`).
- **Les bords communs.** Deux fenêtres qui se touchent se redimensionnent ensemble, jamais sous leur taille
  minimale.
- **Le rack et l'historique** défilent à la molette et au clic-molette ; F montre le canal choisi, ou l'entrée
  en cours.
- **L'historique**, le plus récent en haut, montre ce qui s'ajoute quand il est en haut, et garde ses lignes
  sous la main quand tu l'as descendu.
- **Le code :** le calcul est pur (`ui::snap`, `ui::listScroll`), testé sans JUCE ; `PageWindow` le demande,
  `WorkspaceView` place les voisines.

**Preuve :** `--verify-canvas`, 131 sur 131, dont sept étapes neuves :
1. le rack posé sur le bord du bureau ;
2. l'historique posé sur le rack ;
3. le bord commun tiré ;
4. le rack à la molette, au clic-molette et à F ;
5. l'historique qui défile ;
6. l'historique qui garde ses lignes quand une entrée s'ajoute ;
7. l'historique qui suit en haut, et F.

## 9. Le `--verify` complet, avec copilote

`main`, Release, dossier et disposition jetables, copilote lancé avec ta clé : **658 passées, 9 en échec, en 6
étapes, aucune neuve** :

| Étape | Cause (déjà au bilan de la S20) |
|---|---|
| 96 | Le crescendo de vélocité : 63 au premier coup, pas 60 |
| 150 | « sombre » est devenu un mot de retouche, l'étape S16 attend « ignoré » |
| 171 | La phrase de la retouche S15 |
| 183–185 | Trois étapes S18 de la toile hors de l'écran sur ce long projet |

Les 26 échecs « copilote absent » de la S20 ont disparu : le copilote tournait.

## 10. Les écarts à l'acquis, dits

- **Un composant moteur neuf, `AudioOutputKeeper`**, et un comportement neuf : le logiciel change de sortie tout
  seul. Choisi par toi en cours de semaine, après l'exposé.
- **Le compteur de blocs de `MeterTapPlugin`** : un atomique de plus, écrit par le fil audio, jamais remis à
  zéro.
- **Deux options neuves de la ligne de commande** : `--verify-lecture` (une vérification) et `--mix-once` (pas
  une vérification).
- **`TransportClock` gagne deux méthodes** (`outputNotice`, `outputLost`) : l'écran apprend l'état de la carte
  par l'horloge, qui est déjà sa porte vers le moteur.
- **`MixSession` lit les jetons en camelCase**, comme le reste du protocole.
- **Un commit parti sans clang-format** (`d4a7e4d`), rattrapé par `120adf9`.
- **Un chantier sur une branche non fusionnée** (§7).

## 11. Ce qui s'est mal passé, dit

- **Un binaire Release qui plantait au démarrage** (division par zéro), le Debug non. Une recompilation complète
  l'a fait disparaître. C'est la deuxième fois, après la S20 : **la compilation incrémentale laisse passer un
  objet périmé, cause non trouvée.** Depuis, je lance l'application une fois après chaque compilation, avant de
  m'y fier.
- **Une étape de vérification mal posée, deux fois** : l'historique à côté du rack sans la place pour lui, et
  une lecture de l'historique faite avant son message asynchrone. Corrigée dans la vérification, pas dans le
  code.
- **La piste compagne tombée** après une journée de travail (§7).
- **Une écriture de fichier par PowerShell a abîmé des accents** pendant le test cassé une fois du domaine,
  puis ajouté un BOM à un `CMakeLists.txt`. Restauré ; je n'édite plus par `Set-Content`.
- **Un cas de test préexistant échoue chez toi** : l'identité CLAP. L'engine des tests garde dans ses réglages
  un chemin d'une ancienne copie du dépôt (`C:\dawS9\…`). Ce n'est pas un défaut du code, mais les tests
  partagent un état de la machine : c'est une dette.
- **Un passage de `--verify-lecture` s'est arrêté seul à son deuxième cycle** (fermeture normale dans
  `daw.log`, pas de rapport). Pas reproduit ensuite. Si c'était toi qui fermais la fenêtre, dis-le-moi.

## 12. La règle du test cassé une fois

Chaque test neuf passé du premier coup a été cassé une fois, et tous sont passés au rouge :
- **Domaine :** la marge (tri inversé), le filtre du recouvrement, le magnétisme (portée, taille minimale), le
  défilement (molette, cadrage, suivi de l'historique).
- **Application :**
  - le gardien de la carte son : 71 muettes sur 75 ;
  - le magnétisme, la molette du rack et le suivi de l'historique coupés : 8 vérifications au rouge dans
    `--verify-canvas` ;
  - la citation de la marge dans `--verify-mix` : au rouge.
- **Moteur, sur la branche :** les enregistrements hors de la tranche, au rouge.

Le bogue des jetons a été trouvé par son premier appel réel, pas par un test. Il n'y a pas de test unitaire de
`MixSession` : c'est noté.

## 13. À essayer, dans l'ordre

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

## 14. Reste à faire

- **La piste compagne** (§7) : la cause de la lecture muette sur la branche, puis la fusion.
- **La compilation incrémentale qui laisse un objet périmé** (§11), deux fois maintenant.
- **Les tests du moteur qui partagent un état de la machine** (l'identité CLAP, §11).
- **Un test unitaire de la lecture des jetons**, côté application.
- Les étapes 96, 150, 171, 183–185 de `--verify`, toujours les mêmes.
- Le départ réel d'un casque Bluetooth, à l'oreille (§13, n° 1).
