# Bilan de fin de S13 — DAW IA

**Période :** semaine 13 sur 26. Rédigé le 26 septembre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`. Workflow, navigation et export sont livrés en tête de semaine
(`c6f0a86`, `fb67015`, `e1a0311`, voir le bilan S12). Ce bilan couvre la suite : 7 commits, de `408e4ab` à
`e638385`, CI verte sur chacun.
**Volume depuis le bilan S12 :** 55 fichiers, +5 019 lignes, −50.
**Tests :** 286 cas ctest (domaine, persistance, interface), 82 cas d'engine (rendus audio), 19 cas Python.
**Vérifications par l'application :** 354 dans la liste, aucune en échec (§5).
**Registry :** 55 types de commandes, contre 48 : les 7 commandes `automation.*`.

## En bref

- **Le bug intermittent n'est pas trouvé.** L'instrumentation est en place et reste dans le code (§1).
- **L'automation est livrée** : modèle, commandes, projection dans Tracktion, écran, copilote. Chaque point
  est prouvé au rendu (§2 à §4).
- **Un trou dans la CI est trouvé et bouché.** Quatre tests ne tournaient pas, et l'un d'eux était faux (§6).

## 1. Chantier 0 : le bug intermittent de la lecture

**Symptôme.** Une commande donnée pendant la lecture reste sans effet : Ctrl+Z en S12 (étape 41), un clic sur
S en S13, avec 7 échecs en cascade.

**Instrumentation (`408e4ab`, laissée en place) :**

- **Le bus** signale chaque refus à un écouteur : l'opération, le type de commande, l'erreur, le thread
  appelant et le thread propriétaire, s'il était déjà en train de muter, le geste ouvert, les profondeurs
  d'annulation et de rétablissement.
- **`TransportSync`** garde son dernier événement et son horodatage : « transport.play vu », « moteur parti
  après N ms », « arrêt du moteur porté au domaine ».
- **`PlaybackProbe`** écoute le bus. Il journalise chaque refus, avec le transport du domaine et celui du
  moteur, le dernier événement de `TransportSync` et son âge, et les 8 dernières commandes appliquées.
- **La vérification** écrit l'état de la sonde au premier échec de chaque étape, et la liste des refus du bus
  à la fin de l'étape. Un clic perdu est noté aussi : bouton désactivé, invisible, ou sous un composant modal.
- **Espace, le bouton lecture et le bouton stop** journalisent leur décision (`ui: Space -> stop/play`, avec
  l'état du moteur et celui du domaine).

**Passages.** Dix passages complets de `--verify`, avec et sans copilote. Le bug décrit ne s'est pas
reproduit. Le bus n'a refusé aucune commande pendant une lecture.

**Un indice, un seul.** Au premier passage, l'étape 62 (« Espace arrête ») a échoué. La sonde montre ceci :

1. Un `transport.stop` est arrivé 23,7 s après le `transport.play`.
2. Il ne venait pas de `TransportSync` : son dernier événement était « moteur parti ».
3. L'Espace de l'étape 62 a donc relancé la lecture au lieu de l'arrêter, parce que le moteur était déjà à
   l'arrêt.

C'est la même famille que les deux échecs connus : l'état attendu n'est plus celui du moteur. À ce moment,
Espace et les boutons n'étaient pas encore journalisés. On ne sait donc pas qui a envoyé cet arrêt. Ce peut
être aussi une touche ou un clic sur le bureau pendant que la vérification tournait. Au prochain échec, le
journal (`daw.log`) le dira.

**Décision.** Comme demandé, je m'arrête là et je passe à l'automation. La boucle de passages continue
d'utiliser la même instrumentation.

## 2. L'automation : le modèle retenu, et pourquoi

Le modèle a été validé le 26 septembre avant d'être codé.

| Choix | Pourquoi |
|---|---|
| Les lignes appartiennent au **projet**, pas aux patterns | Un fondu ou un pan qui traverse le morceau se place dans l'arrangement, comme les clips d'automation de FL posés dans la playlist. Un pattern répété huit fois ne répète pas son fondu. |
| **Une ligne par cible** : volume ou pan d'une tranche (pistes, bus, master), paramètre d'un plugin | Deux lignes sur un même fader donneraient deux réponses à « que vaut-il ici ? ». |
| Points en **temps musicaux** ; valeurs dans les unités du domaine (dB, pan −1…+1, paramètre 0…1) ; courbe −1…+1 | Le point reste sur son temps quand le tempo change. La courbe reprend celle de Tracktion, donc ce qu'on dessine est ce qu'on entend. |
| **Retrait en cascade** : une piste, un bus ou un plugin retiré emporte ses lignes, et l'annulation les rend | Une ligne sans cible n'a pas de sens. Le Ctrl+Z doit rendre le projet à l'octet près. |
| **7 commandes** : `create_line`, `remove_line`, `add_point`, `move_point`, `remove_point`, `set_curve`, `write` | Les six premières sont les gestes de la souris. `move_point` et `set_curve` fusionnent au sein d'un geste. `automation.write` remplace les points d'une plage en une seule entrée : c'est la commande du copilote. |
| Un paramètre automatisé **n'écrit rien dans le projet** en bougeant | Le `ParameterBridge` ignore un paramètre qui a des points d'automation. Sinon, chaque bloc audio écrirait une commande. |
| **Pas de migration** : le champ `automation` n'est écrit que s'il y a au moins une ligne | Les projets des semaines passées restent identiques à l'octet. |
| **Rampe de tempo exclue** | Hors périmètre cette semaine. |

**Projection (`7417f12`).** À chaque réconciliation, le `ProjectProjector` réécrit les courbes Tracktion
qui ont changé, en secondes, selon la carte de tempo :

- le volume est écrit en position de fader ;
- un segment qui traverse un changement de tempo est coupé à ce changement ;
- en mode pattern, aucune automation ne joue : le pattern s'écoute tel qu'il est écrit.

Les tests d'engine mesurent le rendu fenêtre par fenêtre :

- volume descendant : −17,3 à −48,6 dB, une fenêtre par seconde ;
- pan : les deux canaux se croisent (gauche −13,2 → −32,2 dB, droite −32,2 → −13,3 dB) ;
- fondu du master sur les deux dernières mesures : −16,2 → −51,3 dB ;
- point après un changement de tempo : coupé à 4,25 s à 120 BPM, à 8,25 s à 60 BPM ;
- paramètre d'un plugin CLAP automatisé : entendu, et encore là après le retrait du plugin puis son
  annulation.

## 3. L'écran (`4ad4668`)

**Clic droit sur un curseur.** Un clic droit sur un fader ou un pan (mixer, liste des pistes) ouvre sa ligne.
Si elle n'existe pas, il la crée vide, en une seule entrée. Puis il amène la playlist devant et allume la
ligne. Le clic droit ne bouge jamais le curseur.

**Dans la playlist.** Chaque ligne a sa propre piste, sous la piste audio de sa tranche. Les gestes sont ceux
de la ligne de tempo :

- un clic pose un point, calé sur le temps (Maj : position libre) ;
- le glissé qui suit bouge ce point sur les deux axes, en un seul geste ;
- un clic droit ou un double-clic sur un point le retire ;
- la molette change la valeur d'un point (Alt : la courbe) ; un tour de molette donne une seule entrée ;
- un clic droit sur le nom de la ligne propose « Supprimer la ligne ».

Le volume est dessiné en position de fader : la ligne dessinée est la ligne entendue.

**Pas encore à l'écran :** le clic droit sur un paramètre dans la fenêtre d'un plugin. Cette fenêtre est
celle du plugin, pas la nôtre. Le modèle, la commande et le copilote le permettent déjà, et le test d'engine
le prouve.

## 4. Le copilote (`31b095e`, `f1eabab`)

- `automation.write` et `automation.remove_line` sont dans la table des outils. Les gestes fins
  (`add_point`, `move_point`, etc.) n'y sont pas : pour le copilote, une ligne s'écrit d'un coup.
- Le résumé du projet contient les lignes d'automation, quand il y en a.
- **Preuve**, dans la vérification, avec le vrai copilote. La demande « fais un fade-out du master sur les
  quatre dernières mesures » donne :
  - la réponse « de 0 dB à la mesure 33 jusqu'à −100 dB à la fin (mesure 37) » ;
  - une seule entrée d'historique, marquée copilote ;
  - au rendu, mesure par mesure, par rapport au rendu sans automation : 0 ; −2,2 ; −8,6 ; −17,9 ;
    −34,3 dB ;
  - un Ctrl+Z qui rend le projet à l'octet près.

## 5. Vérifications (`e638385`)

La liste compte **354 vérifications**, aucune en échec. Les 15 étapes nouvelles passent par la souris et le
clavier, comme un utilisateur :

| Étape | Mesure |
|---|---|
| Fondu du copilote | voir §4 |
| Clic droit sur le fader du master, deux clics, un glissé, un point retiré au clic droit et un au double-clic | une entrée par geste ; au rendu, chaque mesure est plus basse que la précédente (−0,5 → −78,5 dB) |
| Clic droit sur le pan du Kick, point à gauche puis à droite | les canaux se croisent au temps 72,4 ; milieu attendu : 72 |
| **Désordre :** tempo divisé par deux sous les points | croisement toujours au temps 72,4 : les points suivent les temps, pas les secondes |
| **Désordre :** trois Ctrl+Z puis trois Ctrl+Y pendant la lecture | chacun agit, la lecture continue, le projet revient à l'octet près |
| Tout défaire | plus aucune ligne, projet d'origine à l'octet près |

**Préparation du morceau.** Le morceau de la liste ne sonne que sur la première mesure de chaque pattern. Un
fondu de ses quatre dernières mesures ne baisserait donc que du silence. L'étape recopie le premier temps de
chaque clip sur les mesures suivantes, en un seul groupe, défait à la fin.

**Désordre non couvert par `--verify` :** automatiser le paramètre d'un plugin, puis retirer le plugin. La
vérification n'a pas de plugin tiers à charger. Le test d'engine le couvre avec le plugin CLAP de test (§2).

## 6. Un trou dans la CI

ctest reçoit le nom d'un test doctest comme une liste. Un point-virgule dans le nom le coupe, et le filtre
ne désigne alors plus aucun cas. doctest n'en lance aucun, et le test « passe ».

**Conséquence.** Quatre tests ne tournaient pas en CI. L'un d'eux était la liste épinglée de ce qui manque au
copilote pour mixer. Elle était fausse depuis l'ajout de `automation.write`, et la CI restait verte.

**Correction (`c82e165`) :**

- les noms n'ont plus de point-virgule ;
- la liste épinglée ne contient plus `automation.write` ;
- `check_hygiene.py` refuse désormais un nom de test qui contient un point-virgule.

## 7. À vérifier à l'œil et à l'oreille

1. **Clic droit sur le fader du master** dans le mixer (F10). La playlist doit passer devant, avec une piste
   « Master · Volume » allumée. Le fader ne doit pas bouger.
2. **Deux clics dans cette piste** (haut à gauche, bas à droite), puis lecture en SONG. Le son doit baisser
   régulièrement, sans marche ni clic audible.
3. **Tirer un point** vers le haut et le bas, puis vers la gauche et la droite. La ligne doit suivre la
   souris, et un Ctrl+Z doit défaire tout le glissé.
4. **Molette sur un point**, puis Alt+molette. La valeur change par pas de 1 dB. La courbe se creuse puis se
   bombe.
5. **Clic droit sur le pan d'une piste**, un point à gauche et un point à droite, au casque. Le son doit
   traverser d'une oreille à l'autre.
6. **Demander au copilote** « fais un fade-out du master sur les quatre dernières mesures ». Écouter la fin,
   puis faire Ctrl+Z : la ligne doit disparaître.
7. **Changer le tempo** avec des lignes posées. Les points doivent rester sur leurs temps dans la playlist, et
   le fondu doit finir avec le morceau.
8. **Le fader d'une tranche automatisée** reste où il est pendant la lecture : il montre la valeur du
   projet, pas celle de la ligne. Le déplacer crée une entrée d'historique, mais ne s'entend pas tant que la
   ligne existe. C'est la conséquence du choix du §2 : à juger à l'usage.
9. **L'aspect des pistes d'automation** (hauteur, couleur de la ligne, lisibilité des points) : à juger. Les
   captures `s13-ligne-master.png` et `s13-lignes-master-et-pan.png` du rapport de vérification les montrent.

## 8. Reste à faire

- Le bug intermittent (§1). Au prochain échec, lire `daw.log` : `ui: Space`, `bus refused`,
  `transport: engine stopped on its own`.
- Le clic droit sur un paramètre dans la fenêtre d'un plugin (§3).
- La rampe de tempo.
- S14 : la génération locale dans le piano-roll, comme prévu au bilan S12.
