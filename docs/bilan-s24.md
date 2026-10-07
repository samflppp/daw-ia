# Bilan de fin de S24 — DAW IA

**Période :** semaine 24 sur 26. Travail des 6 et 7 octobre 2026 ; bilan rédigé le 7 octobre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`. Commits S24 de `865f80b` à `237ef6a`, plus ce bilan.
**Où le travail a été fait :** ta machine, Windows, compilé par `scripts/build.cmd` (page de code 65001), Release ;
vérifications sur ta carte (Realtek, 48 kHz), chacune dans un dossier et une disposition jetables, avec un dossier
de réglages de la machine à elle (§2.3) : ni ta disposition, ni ton `Settings.xml`, ni ton index de samples
réécrits.
**Tests :** domaine, 516 cas (+37) ; moteur, 121 (+15) ; Python, 63 (inchangé).
**CI :** chaque commit poussé seul, après le vert du précédent ; aucune rouge cette semaine.
**Vérifications (ta machine, Release) :**
- `--verify` complet, avec copilote : **674 sur 674** au second passage ; le premier en donnait 667, les 7 échecs
  tous des fenêtres non vues affichées pendant une minute (§10) ;
- `--verify-lecture` : **0 lecture muette sur 75**, sur le code final, avec les prises du flux dans chaque chaîne
  (sans copilote : ses 8 étapes du copilote échouent, comme dans chaque passage sans lui) ;
- `--verify-audio` (neuve) 29 sur 29 ; `--verify-flux` (neuve) 131 sur 131 ; `--verify-kit` (neuve) 27 sur 27.

**Registry :** 62 types (+1, `plugin.move`). **Schéma du projet :** 7, inchangé.

## En bref

- **La fenêtre « Audio » (F12)** : pilote, sortie, tampon, latence mesurée, et un conseil **mesuré** sur ta carte
  (« Tester ma carte »). Sur ta Realtek, le conseil reste le partagé à 480 : le mode exclusif décroche (§3).
- **Les effets se posent, se déplacent, se contournent depuis la tranche du mixer** ; un verbe neuf, `plugin.move`,
  déplace un effet sans le recharger (§4).
- **Le flux audio (F3)** : le routage réel du morceau en graphe, le son à chaque endroit en direct, l'avant et
  l'après d'un effet (formes d'onde, spectres), les gestes du graphe par les commandes existantes, l'écoute seule
  d'un état, la proposition du mixage vue dans le graphe avec le son de son essai à blanc (§5).
- **Le kit, version « assembler »** : tes samples mesurés une fois sur la machine, un kit choisi par des règles
  (808 sur la tonique à ±15 cents, un kick qui lui laisse le grave, une couleur commune), écouté, posé en un
  Ctrl+Z (§6).
- **Les bus intelligents** : le même égaliseur sur six pistes devient un bus de groupe au même son (−106,7 dB
  d'écart au rendu) ; le même compresseur aussi, en le disant : « le son change » (§7).
- **Une fausse alerte trouvée et fermée** : les « lectures muettes » de `--verify-lecture` avec les prises du flux
  étaient des relevés tombés dans un silence du morceau (§5.2).
- **À trancher par toi :** la contrainte de couleur du kit sur une petite bibliothèque (§6.4) ; une réverbération
  de DAW IA ou non (§7.3).

## 1. Tes décisions, avec leur date

| Date | Décision |
|---|---|
| 6 octobre 2026 | Le diff trouvé en début de session vérifié et commité ; la branche inconnue supprimée, avec une autre (« oui, supprime les deux »). |
| 6 octobre 2026, après l'exposé | `plugin.move` : oui. |
| 6 octobre 2026, après l'exposé | Écouter seul un endroit du flux : oui. |
| 6 octobre 2026, après l'exposé | Le kit choisi par des règles seules, sans modèle : oui. |
| 6 octobre 2026, après l'exposé | Les bus intelligents : oui, pour la réverbération et le délai par envois, **et pour les égaliseurs et les compresseurs aux mêmes paramètres** — ajout validé : par un bus de groupe (`track.set_output`), quand l'effet est le dernier de sa chaîne, que les pistes vont au même endroit et n'envoient nulle part ; un égaliseur prouvé identique (écart au rendu sous −90 dB) ; un compresseur proposé comme « compression de groupe : le son change », l'écart mesuré. |
| 6 octobre 2026, après l'exposé | Une sortie choisie dans la fenêtre « Audio » devient celle du gardien ; repli : la sortie choisie, celle par défaut de Windows dans le même pilote, puis « Windows Audio » partagé. |
| 6 octobre 2026 | Le conseil mesuré : « Tester ma carte » essaie chaque réglage ; le conseil est la latence de la touche à l'oreille la plus basse au pire, parmi les réglages sans décrochage, 256 échantillons ou moins de préférence ; les essais gardés sur la machine, par carte. |
| 7 octobre 2026 | « Fini la session précédente qui s'est arrêtée » : la suite des étapes 12 à 15 et ce bilan. |

## 2. Le chantier 0

1. **`--verify-lecture`** (la dette S23) : **0 lecture muette sur 75** avec le jeu, et 0 avec `--sans-jeu`, sur ta
   carte. Le jeu n'en ajoute pas ; l'écart de la S23 venait de la sortie fictive du conteneur Linux.
2. **`--verify` complet avec copilote** : **674 sur 674** au début de la semaine. La dette « pas relancé depuis la
   S22 » est fermée. Un échec isolé (« la page Navigateur est ouverte ») vu une fois dans un passage avec le jeu,
   jamais reproduit : cause non trouvée (§10).
3. **`--verify-jeu`** : 46 sur 46. Partagé, 480 échantillons (10 ms) : de la touche au premier échantillon rendu
   **12,5 ms mesurés**, plus 10 ms que la carte déclare, **22,5 ms** de la touche à l'oreille. Raw Input : son
   enregistrement est prouvé (la ligne du journal n'est écrite qu'après succès) ; une vraie frappe ne l'est pas,
   la vérification envoie les scan codes elle-même.
4. **`direction.amount`** ignoré par les décalages de couleur des règles du mixage : corrigé (`4fb3fbd`).
5. En plus : un accord tenu lu à 114 BPM par la direction, corrigé (`865f80b`).

### 2.3 Les réglages de la machine, jamais réécrits par une vérification

Tracktion garde la carte et son tampon dans ton `%APPDATA%\DAW IA\Settings.xml`, que toutes les vérifications
partageaient. Depuis `921139c`, toute vérification (`--verify…`) travaille dans `<dossier>/reglages-machine`, rempli
d'une copie de `Settings.xml` et de `plugins.xml`. `--verify-audio` le prouve : ton `Settings.xml` est identique à
l'octet après un passage qui change trois fois de pilote et de tampon. L'index des samples du kit y vit aussi
pendant une vérification ; `--verify-kit` prouve que le tien n'est pas touché.

## 3. La fenêtre « Audio »

- **F12.** Le pilote, la sortie, le tampon (en échantillons et en ms), la fréquence, relus au moteur ; la latence
  sur trois lignes, chacune dite pour ce qu'elle est : **mesurée** de la touche au premier échantillon (l'entrée
  du jeu), **mesurée** le rythme réel des blocs et les décrochages (un rappel audio à part, `BlockClock` : un bloc
  arrivé plus de 1,5 bloc après le précédent est un décrochage), **déclarée** la latence de sortie.
- **Un réglage qui échoue** (erreur, ou aucun bloc pendant 500 ms) rend l'ancien et le dit.
- **Changer de tampon pendant la lecture** : le jeu est relâché avant la réouverture, et le transport ne s'arrête
  plus (`e14f650` : dans la seconde et demie qui suit, l'arrêt de Tracktion n'est pas porté au domaine ; le moteur
  repart 200 ms après que la carte rejoue, environ 480 ms après la note tue sur ta carte).
- **Le conseil est mesuré.** Ce que JUCE offre vraiment sous Windows, sur ta Realtek :

| Pilote | Tampons | Décrochages en 3 s | Au pire |
|---|---|---|---|
| Windows Audio (partagé) | 480 seul | 0 | — |
| Windows Audio (Low Latency Mode) | 480 seul sur ta carte | 0 | — |
| Windows Audio (Exclusive Mode) | 144, 192, 256, 480 | une quarantaine à chaque tampon | 75 ms |

  Le conseil y reste **le partagé à 480**. Un conseil tiré des seuls noms aurait conseillé l'exclusif à 256 : il a
  été remplacé par la mesure (décidé le 6 octobre 2026). L'essai est gardé sur la machine (`carte-audio.json`),
  par sortie.
- **`--tampon` et `--pilote partage|basse-latence|exclusif`**, en vérification seulement : `--verify-lecture
  --pilote basse-latence` perd la carte quinze fois, la rouvre chaque fois, **0 sur 75** ; en exclusif à 256,
  **1 sur 75**, cohérent avec ses décrochages, non prouvé.
- **`--verify-audio`**, 29 sur 29 : l'essai, le tampon relu au moteur et mesuré sur les blocs (256 en exclusif),
  la latence affichée égale à la mesurée, un tampon changé note tenue, une sortie inexistante refusée, ton
  `Settings.xml` intact.

## 4. Les effets depuis la tranche du mixer

- **`plugin.move`** (`87f85b6`) : déplacer un effet dans sa chaîne garde l'instance (identifiant, état, paramètres,
  lignes d'automation) ; le moteur déplace le plugin qu'il tient au lieu d'en recharger un. Rendu identique
  (−240 dB) à une chaîne posée directement dans le nouvel ordre. Entre deux pistes : retirer puis poser, en un
  groupe.
- **Les emplacements de la tranche** (`80f64ff`) : un point (joue / contourné), le nom et ce qui s'en dit sans
  l'ouvrir (la courbe de l'égaliseur de DAW IA, seuil et ratio du compresseur), glisser pour déplacer, double-clic
  pour ouvrir, clic droit pour contourner ou retirer, « ＋ effet » pour poser.
- **Au rendu** (`--verify-flux`) : l'égaliseur contourné donne le rendu sans effet à −220 dB ; jouant, il s'en
  écarte de −8,8 dB.
- **La page de la chaîne de plugins reste** ; elle fait double emploi avec la tranche, à trancher en phase
  d'essais. Rien n'est retiré.

## 5. Le flux audio

### 5.1 Ce qui est construit

- **Le graphe** (`flux::graphOf`, domaine) : des états du son (la source d'une piste, la somme d'un bus et du
  master, l'état après chaque effet, après le fader) reliés par des effets, les sorties et les envois avec leur
  niveau. Disposition : une colonne par plus long chemin, une ligne par piste, un bus à la ligne moyenne de ce qui
  l'atteint, jamais sur la ligne d'une sortie qui le traverse (`da583e7`, trouvé à l'écran). Calculé à chaque
  changement, jamais stocké.
- **Les enregistrements** (`1cf600d`) : ils jouent sur la piste compagne, qui ne porte que les effets de DAW IA.
  Quand une piste a des enregistrements et un plugin à toi, le graphe le montre : une branche « enregistrements »
  qui ne passe que par les effets de DAW IA et rejoint la piste au fader. Le graphe dit la dette S20, il ne la
  cache pas.
- **Les prises** (`e9ebe54`) : un plugin de la projection à chaque état de chaque chaîne. Armé (visible), il écrit
  le son mono dans un anneau indexé par la position de l'Edit ; sinon il lit un atomique et rend la main. Le retard
  d'un plugin est compensé sans rien faire : Tracktion recule la position de chaque plugin de la latence en amont,
  l'avant et l'après se lisent au même instant (prouvé avec un plugin à 2 400 échantillons de latence).
- **La fenêtre (F3)** : chaque état montre sa forme d'onde sur 1,25 s et son niveau ; un clic sur un effet ouvre
  dessous son avant et son après, formes d'onde superposées et spectres. Navigation de la toile : Ctrl+molette
  autour de la souris, molette, clic-molette, F.
- **Les gestes** (`8c7777d`), par les commandes existantes : effet glissé sur un lien de sa chaîne (`plugin.move`)
  ou d'une autre (retirer puis poser, un groupe) ; clic droit sur un lien (le menu « ＋ effet ») ; un trait de la
  sortie d'une tranche à un bus (`track.set_send`, −12 dB) ; le bout d'une sortie glissé sur un bus
  (`track.set_output`) ; la pastille ; Suppr ; clic droit dans le vide (« Nouveau bus »).
- **Écouter seul** (`27c3010`) : un clic sur un état le fait entendre seul, mono, à la place du morceau ; un second
  clic, Échap ou la fenêtre cachée rend le morceau. Rien n'est écrit. Au vu-mètre du master : la sortie de la
  source seule −15,1 dBFS, la somme de « Réverb » seule −27,1 dBFS, **12,0 dB plus bas, comme l'envoi**. Un rendu
  hors ligne (un export) ne joue jamais l'écoute.
- **La proposition du mixage dans le graphe** (`73307b1`) : tant qu'elle est prête, le graphe est celui de l'état
  proposé ; ce qu'elle ajoute en pointillé, ce qu'elle change entouré avec sa phrase ; le son de chaque état est
  celui que les prises de la copie de l'essai à blanc ont entendu, depuis la tête de lecture. Mesuré, pas simulé.
- **Le coût des prises** (`33279ce`), mesuré sur le fil audio, 30 s chacun, sans compilation en cours mais
  **machine pas garantie au repos** (charge Tracktion 0,025) : 10 prises armées, **0,255 % d'un cœur**, 2,55 µs par
  bloc et par prise ; désarmées, 0,015 %. L'exposé estimait 0,5 % pour 30 nœuds. Aucun bloc n'attend l'écran : la
  prise écrit, la fenêtre lit, aucun verrou.

### 5.2 Les « lectures muettes » qui n'en étaient pas

Avec les prises dans chaque chaîne, `--verify-lecture` donnait 35 à 41 lectures muettes sur 75. Rendre les prises
inertes ne changeait rien (41), remettre la limite de plugins de Tracktion non plus (35), retirer celles du master
réduisait à 7. **Les 114 relevés muets de cinq essais tombaient tous entre 0,55 et 0,65 s** : à 90 BPM, le kick du
projet s'éteint avant le temps suivant, et la crête des 300 dernières millisecondes y passe sous −60 dBFS (mesuré
sur le rendu). Le relevé lisait le master à une heure — 500 ms après le premier son, plus la capture d'écran —,
pas à une position. Il lit maintenant la dernière lecture faite entre 0,2 et 0,55 temps (`8a0c6be`) : **0 sur 75**
avec les prises ; placé dans le trou, 75 sur 75.

**Pourquoi les prises décalaient le relevé d'environ 50 ms : non trouvé.** Pas une prise qui calcule (inertes, le
décalage restait), ni une réconciliation qui referait les prises (une seconde réconciliation ne touche aucun
plugin, testé).

## 6. Le kit, version « assembler »

### 6.1 Ce qui est mesuré, où, quand

- **Par sample** (`b0ae156`, domaine) : longueur jusqu'à −60 dB, attaque (10 à 90 % du pic), queue (−10 à −40 dB),
  niveau, crête, largeur et dix octaves (les mesures du mixage), centroïde, part au-dessus de 5 kHz, profil grave de
  20 à 160 Hz par sixièmes d'octave, hauteur (YIN toutes les 10 ms, la médiane du corps, les cents, la glissade
  dite à part, la classe de hauteur retenue sous 100 Hz).
- **Le rôle** : le nom et les dossiers proposent, le son confirme ou refuse (un charley ouvert dure plus de
  250 ms ; une 808 a une hauteur ; un kick et une 808 vivent sous 2 kHz, un charley au-dessus).
- **L'index** (`6a96861`) vit sur la machine, `samples-index.json` à côté de `Settings.xml`, jamais dans le projet.
  Un sample est reconnu à son chemin, sa taille, sa date et la version de la mesure. Hors du fil des messages, sur
  plusieurs fils, annulable, avec sa progression.
- **Ce qu'il coûte**, sur 10 000 fichiers construits de 0,25 s, 7 fils, machine pas garantie au repos : **27,7 s la
  première fois (2,77 ms par fichier), 1,83 s ensuite**. La première mesure prenait 320 s : YIN coûte la fenêtre
  fois la plus longue période ; il lit maintenant le son décimé à 12 kHz (`6013db6`). L'exposé estimait 30 à 90 s :
  l'estimation était fausse d'un facteur dix avant la décimation.

### 6.2 Le choix

Par des règles, sans modèle (décidé le 6 octobre 2026), déterministe : la 808 sur la tonique du projet à ±15 cents,
sinon la quinte, sinon rien et la plus proche dite (« aucune 808 en fa# ni sur sa quinte ; la plus proche : … ») ;
le kick dont le grave laisse la place à la 808 (corrélation des profils graves sous 0,5, au moins un quart d'écart
entre son pic grave et la hauteur de la 808) ; puis la caisse claire (sinon un clap), les charleys, une
percussion ; chacun à moins de 0,75 des autres sur trois axes. Les axes (sombre ↔ brillant, sec ↔ ample,
propre ↔ saturé) sont des scores z dans le rôle ; « saturé » est une approximation (la crête et l'aigu). Par défaut,
la direction place les axes en proportion de son `amount` ; **les échelles de cette conversion sont un choix**
(10 dB d'aigu de plus que de grave valent +1 en brillance ; une part de côté de 0,05 au-dessus de 0,1, +1 en
ampleur), à régler à l'oreille.

### 6.3 La fenêtre, l'écoute, la pose

La page « Kit » (`71c46d4`) : mesurer, régler les axes, composer ; chaque élément dit pourquoi, nombres compris ;
deux mesures mixées en mémoire au tempo du projet jouées par la prévisualisation du navigateur, ou un élément seul ;
poser : une piste sampler par élément, un groupe. `--verify-kit`, 27 sur 27, sur une bibliothèque construite : 33
rôles sur 33, la 808 en la à 0 cent, un kick à 107 Hz contre 55 Hz (corrélation −0,10), le même kit deux fois,
posé en un pas, un Ctrl+Z à l'octet.

### 6.4 À trancher : la contrainte de couleur sur une petite bibliothèque

Sur une première bibliothèque de quatre kicks, **le kit tombait à trois éléments** : avec des scores z, les
extrêmes d'un petit rôle sont à ±1,2, et aucun kick n'était à 0,75 de la 808 sur les trois axes. Sur une
bibliothèque plus fournie (8 kicks, 6 caisses claires…), six éléments. Une vraie bibliothèque compte souvent des
centaines de samples par rôle, mais une petite existe. Deux voies : **garder 0,75** et dire ce qui manque (fait
aujourd'hui), ou **relâcher la contrainte quand un rôle compte moins de dix samples** (un seuil plus large, ou
l'élément le plus proche dit « hors couleur »). Je recommande la seconde : un kit incomplet apprend moins qu'un kit
entier dont l'écart est dit. Rien n'est changé sans toi.

## 7. Les bus intelligents

- **Le domaine** (`69bde92`) propose et compile en verbes existants (`bus.add`, `plugin.insert`, puis
  `track.set_send` ou `track.set_output` et `plugin.remove` par piste). Réverbération ou délai par envois (comme le
  catalogue range le plugin), jamais un effet de DAW IA ni un effet qu'un autre suit ; l'égaliseur ou le compresseur
  de DAW IA aux mêmes paramètres par un bus de groupe, aux conditions validées. « Proche » (même plugin, paramètres
  à 0,02, autre état) est proposé et dit.
- **La page « Bus »** (`237ef6a`) : chercher, essayer à blanc (avant et après rendus, écart échantillon par
  échantillon, sonie ; pour un envoi, l'effet seul sur un bruit connu, avec et sans lui, et le son sec qu'il laisse
  passer, dit au-delà de −20 dB), écouter avant / après à niveau égal, voir dans le flux, garder en un groupe ou
  refuser.
- **Mesuré** (`--verify-flux`) : six pistes sous le même égaliseur, **−106,7 dB d'écart, le même son**, gardé en un
  pas, un Ctrl+Z à l'octet ; deux pistes sous le même compresseur, **−37,4 dB**, 4,8 puis 4,6 LUFS, la phrase « le
  son change », refusé sans rien écrire.

### 7.3 Ce qui n'est pas éprouvé de bout en bout

**La réverbération par envoi**, ni la mesure du son sec : il faut une réverbération, et DAW IA n'en a pas
d'interne ; une vérification ne dépend jamais de tes plugins. Le critère du brief (« six pistes portant la même
réverbération ») n'est donc prouvé que dans le domaine (la proposition et ses commandes), pas au rendu. Deux voies :
une réverbération interne de DAW IA (un effet de plus que le mixage par l'IA pourrait régler : une décision de
modèle, à t'exposer), ou un test au label `audio` avec un VST3 donné par `DAW_TEST_VST3`, hors CI. Je recommande la
seconde pour la S25, la première ne se décide pas en passant.

## 8. Les tests cassés une fois

Chaque test neuf, et chaque vérification neuve, a été cassé une fois par une mutation du code qu'il regarde, et est
tombé au rouge avant d'être remis. Trois tests étaient aveugles et ont été corrigés avant d'être gardés : le kick
« collé » à la 808 n'était jamais le plus proche des axes (le test passait sans la contrainte) ; un rendu muet
passait la comparaison relative de l'écoute seule (contrôle de niveau ajouté) ; le changement de la date seule d'un
sample ne cassait pas le test de l'index (la taille changeait aussi). Les mutations de chaque commit sont dans son
message.

## 9. Ce que la vérification a trouvé en route

- **Le relevé de `--verify-lecture`** tombait dans un silence du morceau (§5.2).
- **Un bus posé sur la ligne d'une sortie** qui le traverse (§5.1), vu sur une capture.
- **Le fader centré retire 3 dB** (la loi de panoramique de Tracktion) : l'écoute seule de la source se comparait
  mal à la somme d'un envoi parti après le fader. L'étape compare désormais la sortie de la source.
- **Le niveau d'un état proposé** lu sur ses 300 dernières ms passait pour muet quand la fenêtre débordait la fin du
  morceau (`211d275`).
- **Le coût de l'index** dix fois l'estimation (§6.1).
- **F3 sur une page derrière une autre la ramène devant** au lieu de la fermer, comme F5–F7 dans FL : voulu ;
  l'étape de la vérification appuie deux fois.

## 10. Ce qui s'est mal passé, dit

- **La session s'est arrêtée** au milieu de la bissection des lectures muettes ; reprise le 7 octobre 2026, trois
  modifications d'essai (`EXPERIMENT`) retirées avant tout commit.
- **Intermittents, cause non trouvée :**
  - `--verify-flux`, « F3 rouvre la fenêtre » après 30 s fermée : la capture la montre dessinée, la vérification ne
    la voit pas affichée en 3 s ; **deux fois sur une dizaine de passages** ;
  - `MeterTests`, « fader −6 dB », 1 fois sur 4 (probablement la phase de 4OSC, qui change à chaque rendu, non
    prouvé) ;
  - le premier `--verify` complet de fin de semaine : 667 sur 674, les 7 échecs entre les étapes 179 et 232, tous
    des `isShowing()` faux, la barre de titre de la fenêtre principale comprise — la fenêtre entière n'était pas vue
    affichée pendant une minute environ. Le second passage, sans rien changer : 674 sur 674. Peut-être le même
    mécanisme que « F3 rouvre la fenêtre » ; non prouvé ;
  - « la page Navigateur est ouverte » dans `--verify-lecture` avec le jeu, une fois au chantier 0.
- **Deux mutations mal écrites** pendant la semaine (une qui n'appliquait rien, une qui aurait déréférencé `end()`),
  vues avant de conclure ; les passages concernés relancés.

## 11. Les écarts à l'acquis, dits

- **Un verbe de plus, `plugin.move`** (registry 62), décidé le 6 octobre 2026.
- **Des plugins de la projection dans chaque chaîne** : les prises du flux (`FluxTapPlugin`), comme `MeterTap`.
  **Un état global au processus** : `FluxMonitor`, les anneaux de l'écoute seule, pour qu'aucune prise n'en tienne
  une autre pendant que la projection les fait et les défait.
- **Les limites de plugins de Tracktion portées à 128** (4 sur le master par défaut), par un `EngineBehaviour` :
  latentes jusqu'ici, atteintes par les prises.
- **YIN passe du moteur au domaine** (`a9d8952`), déplacé.
- **Les réglages de la machine copiés dans le dossier de chaque vérification**, et `EngineHost` qui dit son dossier.
- **Un index de samples sur la machine** (`samples-index.json`).
- **`TransportSync` relance le moteur** après une carte rouverte ; **Main** détruit la carte son avant le moteur.
- **Les jetons** `metric.mixer.insertHeight`/`insertLines`, `metric.audio.*`, `metric.flux.*`, `color.flux.*`,
  `metric.kit.*`.
- **F3 pris pour le flux sans te le demander** (F1 et F2 laissés à Windows) ; les pages « Kit » et « Bus » sans
  raccourci.
- **L'anneau d'une prise garde 1,37 s d'échantillons**, pas un minimum et un maximum tous les 32 comme exposé ; la
  fenêtre montre 1,25 s, pas 2 s : plus simple, et le spectre en a besoin.
- **Une table de noms des rôles du kit dans le domaine**, à côté de celle des familles du navigateur (interface) :
  l'une range un sample dans un kit, l'autre trouve un mot.
- **Le relevé de `--verify-lecture` lu par position** et non plus à une heure : la vérification a changé, pas le
  logiciel.
- **L'écoute du kit passe par un fichier temporaire** (`%TEMP%\daw-ia-kit-preview.wav`) joué par la
  prévisualisation du navigateur.

## 12. À essayer, dans l'ordre

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

## 13. Reste à faire

- **Trancher** : la contrainte de couleur sur une petite bibliothèque (§6.4) ; la réverbération par envoi au rendu
  (§7.3).
- **Les intermittents** de §10.
- **Le repli de la carte sur le partagé**, jamais éprouvé (aucune vérification ne perd une carte en exclusif).
- **`scripts/verify-quit.ps1`** utilise encore tes réglages (il ne passe pas par `--verify`).
- **Deux contrôles de `--verify-audio` ne peuvent pas tomber seuls** (« le conseil a tenu » lit la même fonction
  que le conseil ; la présence des réglages copiés ne prouve pas la copie).
- **La piste compagne**, toujours sur sa branche ; le flux montre aujourd'hui ce qu'elle laisserait de côté.
