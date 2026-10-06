# Bilan de fin de S23 — DAW IA

**Période :** semaine 23 sur 26. Travail des 5 et 6 octobre 2026 ; bilan rédigé le 6 octobre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`. Commits S23 de `997882d` à `a6bdeec`, plus ce bilan.
**Où le travail a été fait :** un conteneur Linux en ligne, pas ta machine. Pas de `scripts/build.cmd`, pas de
`C:`, pas de carte son, pas de clé d'API, pas de Windows. Compilé en **clang Release** (le job Linux de la CI,
celui qui voit `-Wimplicit-int-float-conversion`), lancé sous un écran virtuel (Xvfb) avec une sortie audio
ALSA « null », cadencée en temps réel par un tuyau pour `--verify-jeu`. Debug et Release dans ton dossier de
build restent à recompiler chez toi.
**Tests :** domaine, 479 cas (+32) ; moteur (label `audio`, ici sous Linux), 106 sur
106 (+7) ; Python, 63 cas (inchangé).
**CI :** **rouge sur `ae36568`** (MSVC, C4324), corrigée par `7e4e8f3` ; ensuite chaque commit attendu au vert avant
le suivant (§12).
**Vérifications (ici, Linux, Release) :**
- `--verify-jeu` (neuve), 46 contrôles, **0 en échec** ;
- `--verify` complet, sans copilote : 630 passés, 36 en échec sur `main` avant la semaine, les mêmes (§2) ;
- `--verify-lecture` : mesurée ici, mais l'environnement fausse la mesure (§7). **À relancer chez toi.**

**Registry :** 61 types, inchangé. **Schéma du projet :** 7, inchangé. **Aucun verbe ajouté.**

## En bref

- **On joue.** Le clavier de l'ordinateur (Ctrl+T, ou le bouton « Clavier » du transport) et tout clavier MIDI
  branché jouent l'instrument de la piste choisie dans le rack, en temps réel, sans rien écrire dans le projet
  (§3).
- **Le clavier se lit par la place des touches**, pas par leurs lettres : un QWERTY joue les mêmes notes aux
  mêmes endroits (§4).
- **On enregistre.** ● (ou Ctrl+R) : une mesure de décompte au clic, puis ce qu'on joue arrive dans la toile, en
  rouge, sans être encore dans le projet. ● de nouveau : la prise est écrite en un seul Ctrl+Z (§5).
- **Une note jouée sur le temps est écrite sur le temps** : la latence de la carte est retirée (§5, §6).
- **Aucune note coincée** dans les cinq cas : piste changée, fenêtre derrière, carte perdue, clavier débranché,
  morceau lancé ou arrêté (§3). En route, deux vraies causes de notes coincées ont été trouvées et corrigées, et
  une troisième, dans 4OSC, est notée (§9).
- **Les sections d'une référence se lisent en mesures du projet** dans le panneau Direction (§2).
- **Tombé :** la fenêtre de réglages audio (pilote, tampon). Le logiciel garde le pilote par défaut de Windows
  (§11).
- **À mesurer chez toi avant de te fier à la semaine :** `--verify-lecture` avec et sans le jeu (`--sans-jeu`), §7.

## 1. Tes décisions, avec leur date

| Date | Décision |
|---|---|
| 5 octobre 2026 | On garde HTDemucs pour le séparateur, sans chercher d'autre modèle pour l'instant. Rien à faire cette semaine. La dette reste telle qu'elle est écrite dans CLAUDE.md §6 (le code est sous MIT, les poids sont donnés pour la recherche). |
| 6 octobre 2026 | « Je valide tout, Raw Input et régulière, go » : le modèle du jeu exposé en début de semaine, validé en bloc, avec deux choix tranchés. |
| 6 octobre 2026, après le bilan | La fenêtre « Audio » tombée cette semaine va à la S24. |
| 6 octobre 2026, après le bilan | La règle du dernier 1/32 de temps au tour de boucle est gardée telle quelle (§5). |

Ce que « je valide tout » couvre, tel qu'exposé :

| Question | Réponse retenue |
|---|---|
| Le chemin du jeu | Une file sans verrou par piste, un plugin interne en tête de chaque chaîne ; rien dans le projet, aucune commande (§3). |
| Le placement d'une note jouée | **Régulier** : chaque note attend la même durée, un bloc et une marge, au lieu de tomber au début du bloc suivant (§3). |
| La piste jouée | Celle du canal choisi dans le rack. Aucune : rien, et l'écran le dit. |
| Le clavier de l'ordinateur | **Raw Input sur un fil à lui**, la place des touches (scan code). La disposition de FL. Un mode, Ctrl+T et un bouton, actif partout sauf dans un champ de texte ; Ctrl, Alt et la touche Windows gardent leurs raccourcis ; F reste le cadrage (§4). |
| Le clavier MIDI | Toutes les entrées, vers la piste choisie ; un réglage de la machine (§5). |
| La prise | Dans le pattern en cours en PAT, la boucle ajoute ; en SONG, un pattern neuf posé là. Commandes existantes, un groupe, à la fin. Aucune quantification ; Ctrl+Q après. La pédale allonge les notes (§5). |
| Le métronome | Le clic de Tracktion, une propriété de l'Edit qui ne vient pas du projet (écart, §13). |

## 2. Le chantier 0

**1. Le `--verify` complet avec copilote.** Il n'a pas pu être lancé : pas de Windows, pas de clé. Lancé ici sans
copilote sur `main` avant la semaine : 630 passés, 36 en échec.

| Échecs | Cause |
|---|---|
| 28 | le copilote : ni clé ni service ici |
| 8 | l'export MP3 et AAC : Media Foundation n'existe que sous Windows |
| 2 (étapes 130, 132) | un clic d'automation à −37,2 dB au lieu de −40, sur une étape de la S13 ; probablement la taille de l'écran virtuel, **non démontré** |

Aucun échec dans ce que les stems ou la direction ont ajouté. **La dette « `--verify` complet pas relancé » reste
ouverte** : seule ta machine, avec la clé, peut la fermer.

**2. La piste compagne.**
- **La demi-journée de la S22 n'a pas eu lieu** : la branche est inchangée et le bilan n'en dit rien.
- **Cette semaine :** une demi-journée de lecture du graphe de Tracktion. Un candidat, écarté par la lecture :
  Tracktion range la destination d'une piste par son numéro (« track 3 »). Lu de près, ce numéro n'est relu qu'à
  des moments où il est juste ; je l'écarte sans preuve par l'expérience.
- **La reproduction ici ne vaut rien** : avec la sortie « null » non cadencée, `main` donne lui-même 9 à 12
  lectures muettes sur 75 (0 chez toi). La branche rejouée sur `main` en donne 14.
- **Cause non trouvée. La branche reste**, non fusionnée.
- **Ce que ça retire au jeu : rien.** Sur `main`, les notes jouées passent par l'instrument de la piste et par
  tes plugins ; seuls les enregistrements audio ne les traversent pas.

**3. Les sections en mesures du projet (`997882d`).**
- Le panneau Direction écrit « A 1–8 », « B 9–24 ».
- Une mesure de la référence est une mesure, quel que soit le tempo du projet ; elle se compte au tempo lu de la
  référence, jamais au tempo tapé à la main.
- Sans tempo lu, les lettres restent en secondes.
- Le copilote lit toujours les secondes : seule la conversion et son affichage, comme demandé.

**4. La preuve du mixage vers une référence est mince : ni le garde-fou, ni la cible.** Réponse écrite, rien
changé.
- **La cible n'est pas timide** : la référence de `--verify-mix` n'a presque pas d'aigus.
- **C'est la règle qui bride.** `baseMix` ne traduit la couleur de la référence qu'en faders (kick et basse pour
  le grave, charleston pour l'aigu), chacun borné à ±3 dB **dans la règle**, plus serré que les garde-fous
  (+6 / −12 dB). Aucune égalisation du master ne vise la référence.
- **La mesure est honnête** : −2 dB est à peu près ce que ces leviers peuvent donner.
- **Un défaut vu en route, pas corrigé** : ces deux décalages ignorent `direction.amount`. À zéro, la référence
  déplace encore ces faders.

## 3. Le chemin du jeu, fil par fil

1. **Le fil d'entrée.**
   - *Clavier de l'ordinateur* : le fil de Raw Input (Windows).
   - *Clavier MIDI* : le rappel MIDI de Tracktion, un par entrée.
   - *Vérification* : le fil des messages.

   Chacun appelle `domain::live::Router` :
   - la note est datée sur l'horloge d'entrée (le compteur de performance de Windows) ;
   - la piste visée est lue (un atomique) ;
   - la note est inscrite dans la table des notes tenues (source, canal, hauteur → piste) ;
   - elle est poussée dans la file de la piste : une file bornée sans verrou (Vyukov, 512 messages), qui refuse
     plutôt que d'attendre ;
   - pendant une prise, elle est aussi poussée dans la file de la prise.
2. **Le fil audio.** `engine::LiveInputPlugin`, posé par la projection en tête de la chaîne de chaque piste,
   avant 4OSC, le sampler ou ton instrument. À chaque bloc, il vide sa file dans le MIDI du bloc, chaque message
   à sa place sur la ligne de temps régulière : joué à t, il sonne à **t + un bloc + une marge**.
   - La marge vaut un quart de bloc, 1 ms au moins : c'est la gigue du rappel audio. Sans elle, une note arrivée
     juste avant un rappel en retard serait rabattue au début du bloc (§10).
   - Deux de ses messages ne partagent jamais un échantillon (§9).
   - Il publie à chaque bloc la position du morceau, pour la prise.
3. **Le fil des messages.** Il choisit la piste (`app::LivePlay` suit la sélection), dit au clavier s'il y a un
   champ de texte ou si la fenêtre est derrière, et vide la file de la prise. **Le jeu ne l'attend jamais.**

**Rien n'est écrit dans le projet.** Aucune commande ne passe par le bus. Prouvé au rendu et dans `--verify-jeu` :
le projet et son historique sont les mêmes à l'octet après avoir joué.

**Allocation et verrous.** Aucun de notre fait sur le fil audio. Deux réserves :
- le tableau MIDI de Tracktion garde sa capacité d'un bloc à l'autre, et peut grandir une fois, à la première
  note ;
- l'écouteur MIDI de Tracktion prend un verrou tournant sur le fil MIDI, pas sur le fil audio.

**Pourquoi pas ce que Tracktion offre :**
- `injectLiveMidiMessage` prend un verrou et ne joint qu'une piste qui a des clips ;
- son entrée MIDI vers une piste prend des mutex sur le fil audio et range le routage dans l'Edit.

**Les notes coincées, chacune finie par un silence mesuré** (au rendu dans les tests du moteur, aux vu-mètres
dans `--verify-jeu`) :

| Cas | Ce qui se passe |
|---|---|
| La piste change pendant qu'une touche est tenue | Le relâché va à la piste qui a reçu l'attaque, quelle que soit la piste choisie ensuite. |
| La fenêtre perd le clavier | Le clavier de l'ordinateur relâche tout ce qu'il tient. Le MIDI continue : un clavier MIDI joue même fenêtre derrière. |
| La carte son part | `AudioOutputKeeper::onLost` vide les files (ce qui attend sortirait en retard, d'un coup), puis relâche chaque note tenue et relève chaque pédale. |
| Un clavier MIDI est débranché | Ses notes et sa pédale sont relâchées. |
| Le morceau part ou s'arrête | Tracktion coupe toutes les voix de l'instrument ; le plugin refrappe les notes encore tenues dans ce bloc. Une note tenue repart de son attaque au lieu d'être coupée. Au tour de boucle, rien n'est coupé : les relâchés et le « all notes off » que Tracktion envoie pour ses clips sont retenus tant qu'une note jouée est tenue. |

## 4. Le clavier AZERTY

**La place, pas la lettre.** Le *scan code* (jeu 1) que Windows donne dans `RAWKEYBOARD.MakeCode`, lu sur un fil à
lui (`app::RawKeyboard`) :
- une fenêtre qui ne reçoit que des messages ;
- `RIDEV_INPUTSINK`, pour qu'un relâché arrive même fenêtre derrière ;
- une attaque n'est prise que si une fenêtre du processus est devant.

JUCE ne donne que des caractères : W et Z changent de place entre AZERTY et QWERTY, ^ est une touche morte.

**La table** (do3 = MIDI 60 en bas, par défaut) :

| Rangée du bas | W | S | X | D | C | V | G | B | H | N | J | , | ; | L | : | M | ! |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| QWERTY | Z | S | X | D | C | V | G | B | H | N | J | M | , | L | . | ; | / |
| Note | do | do♯ | ré | ré♯ | mi | fa | fa♯ | sol | sol♯ | la | la♯ | si | do | do♯ | ré | ré♯ | mi |

| Rangée du haut, une octave au-dessus | A | é | Z | " | E | R | ( | T | - | Y | è | U | I | ç | O | à | P | ^ | = | $ |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| QWERTY | Q | 2 | W | 3 | E | R | 5 | T | 6 | Y | 7 | U | I | 9 | O | 0 | P | [ | = | ] |
| Note | do | do♯ | ré | ré♯ | mi | fa | fa♯ | sol | sol♯ | la | la♯ | si | do | do♯ | ré | ré♯ | mi | fa | fa♯ | sol |

Les cinq dernières touches de la rangée du bas jouent les mêmes hauteurs que les cinq premières de la rangée du
haut, comme dans FL : 37 touches, 32 hauteurs.

**Les deux états :**

| Touches | Mode éteint | Mode allumé |
|---|---|---|
| Lettres, chiffres et ponctuation des deux tables | rien (aucun raccourci n'y est attaché) | des notes |
| F, Maj+F, K | F cadre, Maj+F montre l'ensemble | inchangé : F et K ne sont pas des notes (pas de touche noire entre mi et fa, ni entre si et do) |
| ← / → | rien | octave −1 / +1 (0 à 7) |
| Ctrl+(C, V, B, Q, G, Z, Y, T, R), Espace, Tab, Échap, Entrée, Suppr, ↑/↓, F1–F12 | inchangés | inchangés : avec Ctrl, Alt ou la touche Windows enfoncée, aucune touche ne joue |
| Dans un champ de texte (copilote, Ctrl+G, renommer) | du texte | du texte, toujours |

- **La vélocité** est fixe, 100 par défaut, réglable au curseur « vél. » du transport (molette ou glisser).
- **Le mode** est un réglage de l'écran, éteint à chaque lancement. Il se voit en permanence : le bouton
  « Clavier » allumé, et la ligne du transport (« joue « Lead » · clavier, do3 en bas »).
- **La répétition automatique** d'une touche tenue ne rejoue rien.
- **Un relâché finit la note que sa touche a commencée**, même si l'octave a changé entre-temps.
- **La limite des accords.** Un clavier d'ordinateur ne passe souvent que 2 à 4 touches à la fois, selon sa
  matrice. Certains accords de trois notes ne sortiront pas. Le logiciel n'y peut rien ; un clavier de jeu
  « n-key rollover » n'a pas cette limite.

## 5. L'enregistrement, et ce qui s'enregistre

**Le geste.**
- ● dans le transport, ou Ctrl+R : une mesure de décompte avec le clic (le bouton « Décompte » l'allume ou
  l'éteint), puis la prise.
- ● de nouveau, ou Espace, la finit et l'écrit.
- « Clic » allume le métronome hors des prises.
- **Rien de tout cela n'existait** : ni bouton d'enregistrement, ni décompte, ni métronome.

**Le décompte.**
- **En PAT** : la tête part sur la dernière mesure du pattern ; on entend le pattern et le clic ; la prise
  commence quand la boucle revient au début.
- **En SONG** : une mesure avant le point de départ. Au tout début d'un morceau il n'y en a pas : le domaine
  refuse une position avant zéro, et je ne l'ai pas rouvert.
- Lancé pendant que le morceau joue, l'enregistrement commence tout de suite, sans décompte.

**Où ça s'écrit.**
- **En PAT** : dans le pattern que PAT joue, sur la rangée de la piste jouée, ouverte s'il le faut. La boucle
  **ajoute**, et une note rejouée au même endroit, à 1/128 de temps près, n'est pas doublée.
- **En SONG** : un pattern neuf sur sa propre ligne, de la mesure de la première note à la mesure après la
  dernière.

**La règle du projet.** À la fin de la prise, des commandes existantes en **un seul groupe** :
- `pattern.add_track` si la rangée manque ;
- `note.add` pour chaque note, `pattern.create` et `pattern.place` en plus en SONG ;
- tous les identifiants engendrés par l'appelant.

Un Ctrl+Z retire la prise à l'octet. **Aucun verbe neuf.** Pendant la prise, les notes se voient arriver dans la
toile, en rouge (`color.note.take`), dans chaque bloc du pattern, sans être dans le projet. En SONG, le pattern
n'existe pas encore : la ligne du transport compte les notes.

**Où tombe une note : où elle a été entendue.** Le temps écrit est la position du moteur à l'appui, moins la
latence de sortie que la carte déclare (sous WASAPI, JUCE y compte déjà le tampon). Deux règles :
- **aucune quantification**, la prise reste choisie et Ctrl+Q la quantifie ;
- un départ dans le dernier 1/32 de temps avant la fin de la boucle est le premier temps de la passe suivante,
  joué un rien en avance : il est écrit au début. Écrit à la fin, il serait rogné à presque rien par la fin du
  pattern. Ce n'est pas une quantification, seulement le tour de boucle tranché (§9).

**Ce qui s'enregistre, et ce qui ne s'enregistre pas :**

| Message | Joué en direct | Enregistré |
|---|---|---|
| Note et vélocité | oui | oui |
| Pédale de sustain | oui | **en longueurs** : une note relâchée pédale enfoncée finit quand la pédale remonte |
| Pitch bend | oui | non : il faudrait au domaine des courbes par rangée de pattern, ou des expressions par note |
| Molette de modulation, autres contrôleurs | oui | non : même raison |
| Aftertouch | non | non |

L'ajout au domaine que demanderaient le bend et la modulation n'est pas fait, et je ne le ferai pas sans te
l'exposer.

**Le clavier MIDI.**
- Les entrées sont celles de Tracktion, qui les ouvre (Windows ne laisse pas deux clients ouvrir un port) et les
  cherche maintenant chaque seconde au lieu de quatre.
- Chaque entrée a un écouteur qui pousse dans le routeur sur le fil MIDI. Toutes jouent la piste choisie : rien à
  choisir pour un débutant à un clavier.
- Une entrée débranchée relâche ce qu'elle tenait, et `daw.log` le dit. Rebranchée, elle reprend sa place.
- La ligne du transport la nomme (« MIDI : Arturia »).
- Les pads jouent comme des touches ; les boutons de transport d'un contrôleur ne sont pas lus.

## 6. La latence

**Ce qui est mesuré** : de l'instant où la touche est datée au premier échantillon de la note rendu dans le bloc,
lu dans le plugin du jeu, plus la latence de sortie que la carte déclare.

**Ici (sortie ALSA « null », 44,1 kHz, tampon de 512 échantillons, 11,61 ms)** :
- de la touche au premier échantillon rendu : **14,51 ms**, exactement l'attente régulière (un bloc et sa marge
  de 2,90 ms) ;
- latence de sortie déclarée : 34,83 ms, qui ne veut rien dire pour une sortie fictive.

**Chez toi, rien n'est mesuré.** Ce que la règle donne selon le tampon :

| Pilote, tampon | Attente du jeu (bloc + marge) | Puis la carte |
|---|---|---|
| WASAPI partagé par défaut, 10 ms (480 à 48 kHz) | 12,5 ms | sa latence déclarée, tampon compris |
| 256 échantillons à 48 kHz (5,3 ms) | 6,7 ms | idem |
| 128 échantillons à 48 kHz (2,7 ms) | 3,7 ms | idem |

**Ce que le logiciel propose aujourd'hui : rien.** Aucun réglage audio n'existait, et la fenêtre « Audio »
exposée (pilote, tampon, latence affichée, recommandation) est tombée cette semaine (§11). Le pilote reste celui
que JUCE prend par défaut, « Windows Audio » (WASAPI partagé). ASIO demande le SDK de Steinberg et sa licence, à
signer même gratuite : pas cette semaine.

**Ce qui est prouvé sur le temps.** `--verify-jeu` date des notes à l'instant où le temps 1, 2, 3, 4 et 3 et demi
de la passe suivante sera entendu, compte tenu de la latence déclarée. L'écart écrit au temps est de 0,000 ms.
- **Exact par construction** : la vérification et la prise lisent la même position publiée. Ce chiffre prouve la
  cohérence du calcul, pas l'oreille.
- **Cassé une fois** : la prise sans compensation, au rouge (§10).
- **La vraie preuve** demande un câble qui reboucle la sortie sur l'entrée : elle va aux essais.

## 7. `--verify-lecture` : ce que cet environnement ne peut pas trancher

La règle du brief : `--verify-lecture` à 0 sur 75. Mesuré ici, sur la sortie « null » non cadencée :

| Binaire | Lectures muettes sur 75, passage par passage |
|---|---|
| `main` avant la semaine | 9, 12, 10 |
| avec le jeu | 13, 16, 13 |
| avec le jeu, plugin inerte (il ne fait rien) | 14, 16, 16 |

**L'écart existe ici, cause non trouvée**, et il ne dépend pas de ce que fait le plugin. Ce qui est écarté, et
ce qui ne l'est pas :
- **écarté, par un test du moteur** : un plugin recréé à chaque commande (il reste le même objet, `LivePlayTests`,
  cassé une fois) ;
- **mon hypothèse, non démontrée** : la sortie fictive tourne à environ quatre fois le temps réel (« temps du
  flux 41 s » quand le transport est à 1,8 s). Le vu-mètre n'échantillonne alors que par moments, et un graphe
  plus lourd change ces statistiques.

Cela s'accorde avec deux faits :
- `main` lui-même n'est pas à 0 ici (il l'est chez toi) ;
- sur la sortie cadencée, `main` n'a donné aucune lecture muette sur ses 4 premiers cycles (trop lent pour aller
  au bout).

**Ce que je te demande de lancer, sur ta machine :**
- `--verify-lecture` ;
- puis `--verify-lecture --sans-jeu`. Cette option neuve retire le plugin du jeu des chaînes, sans toucher au
  code : la comparaison se fait sur la même machine.

Si le jeu y montre des lectures muettes et `--sans-jeu` non, c'est une régression à traiter avant tout le reste,
et le jeu se retire d'une ligne (`playLiveFrom`) en attendant.

## 8. Ce qu'on voit

- **Le transport gagne :**
  - ● (rouge allumé pendant la prise et son décompte) ;
  - « Clavier » (allumé quand le clavier de l'ordinateur joue ; éteint et grisé là où Raw Input manque, avec
    sa raison) ;
  - le curseur de vélocité ;
  - « Clic » et « Décompte » ;
  - une ligne : la piste jouée ou « aucune piste choisie », l'octave du clavier, l'entrée MIDI nommée, et
    « Décompte… », « Enregistrement : 5 notes », « Prise écrite : 5 notes. ».
- **La toile** dessine les notes de la prise en cours dans chaque bloc du pattern, en rouge. Elle ne se repeint
  que quand ces notes changent : la toile chargée coûte déjà 9 à 13 ms.
- **Deux couleurs neuves** dans `tokens.json` : `color.note.take` et `color.note.takeOutline`.

## 9. Ce que la vérification a trouvé en route

1. **Une note coincée par le tri de Tracktion.** Une attaque et son relâché à un même échantillon sont triés,
   relâché d'abord : l'attaque reste.
   - Cela arrive quand des messages en retard sont rabattus au début d'un bloc (un accroc de la carte), ou quand
     une touche est enfoncée puis relâchée dans le même appel.
   - `--verify-jeu` l'a trouvé en pressant puis relâchant des touches dans le même appel.
   - **Correctif :** deux messages du jeu ne partagent jamais un échantillon.
   - **Test :** un rendu, cassé une fois (au rouge sans le correctif).
2. **Une note tenue coupée à l'arrêt du morceau.** Au départ, à l'arrêt ou au déplacement de la tête, Tracktion
   pose « tout couper » sur chaque nœud de plugin, hors de portée du plugin du jeu.
   - **Correctif :** les notes encore tenues sont refrappées dans ce bloc.
   - Une première version voyait un saut à chaque bloc arrêté et refrappait au même échantillon qu'un relâché :
     c'était une troisième note coincée, corrigée aussi.
   - **Reste :** un déplacement de la tête *pendant la lecture* ne se distingue pas d'un tour de boucle, qui ne
     coupe rien. Il n'est pas refrappé, et une note tenue y est coupée.
3. **4OSC garde une voix au-delà de 32.** Au-delà de ses 32 voix, il vole une voix, la fait taire en douceur et
   la relance pour la nouvelle note. Si le relâché de la nouvelle note arrive pendant la coupure, la relance a
   lieu quand même : la voix ne s'arrête plus.
   - **Preuve par un rendu :** 33 notes laissent −16 dBFS à la fin, 32 non.
   - C'est un défaut de Tracktion. Il touche aussi bien des notes de clip que le jeu ; au jeu, il tombe sur une
     pédale tenue longtemps.
   - **Non contourné.** Limiter la polyphonie du jeu serait un comportement neuf, à t'exposer ; patcher le
     sous-module est exclu.
   - **Noté aux dettes.**
4. **Le premier temps d'une passe, joué un rien en avance**, tombait à la fin du pattern, rogné à 0,008 temps. La
   règle du dernier 1/32 de temps (§5) le remet au début, entier.

## 10. La règle du test cassé une fois

Chaque test neuf passé du premier coup a été cassé une fois, et tous sont passés au rouge :

- **Domaine — le routeur :**
  - le relâché envoyé à la piste choisie au lieu de celle qui a joué ;
  - le silence qui garde la file ;
  - la pédale oubliée ;
  - la double attaque non fermée ;
  - la note jouée sans piste choisie ;
  - la note refusée gardée tenue ;
  - la prise qui garde la modulation ;
  - la position mal publiée ;
  - la file qui publie la cellule avant de l'écrire (trois fois sur trois).
- **Domaine — la ligne de temps :** sans redémarrage. Le test de gigue est tombé au rouge pour de vrai, avant la
  marge.
- **Domaine — le clavier :** une hauteur de la table, les touches hors table qui jouent, Ctrl ignoré, la
  répétition, le relâché à l'octave courante, la vélocité ignorée, l'extinction qui garde les notes.
- **Domaine — la prise :**
  - les identifiants tirés par la prise ;
  - la latence ignorée ;
  - pas de tour de boucle ;
  - la note tenue non rognée à la fin du pattern ;
  - la pédale ignorée ;
  - les doublons ;
  - la rangée non ouverte ;
  - la SONG non relative ;
  - le premier temps en avance.
- **Domaine — les entrées MIDI** : le premier numéro de source oublié, une source donnée deux fois, un clavier débranché gardé ; cassés seulement en fin de semaine, alors que le message de `fa69eec` les dit déjà cassés (§12).
- **Domaine — les sections :** un arrondi par plancher, le tempo de la correction.
- **Moteur, au rendu :**
  - l'heure du message ignorée ;
  - toujours la première piste ;
  - le relâché qui n'envoie rien ;
  - le silence sans relâché ;
  - les coupures des clips gardées ;
  - le plugin recréé ;
  - deux messages au même échantillon.
- **`--verify-jeu`** : le relâché envoyé à la piste choisie (5 contrôles au rouge), la prise sans compensation de latence (1), le focus ignoré par le clavier (3).

Le test « rien joué, rien écrit » de la prise a été retiré plutôt que cassé : rien ne pouvait le faire tomber
honnêtement.

## 11. Ce qui est tombé, et où ça va

- **La fenêtre « Audio »** : pilote, sortie, tampon, latence affichée et recommandation (« Windows Audio (Low
  Latency) » si elle est là, sinon le mode exclusif, en prévenant que les autres logiciels se taisent ; 256
  échantillons au plus). **Elle va à la S24**, décidé le 6 octobre 2026 après ce bilan : le réglage par défaut
  de Windows donne au jeu 12,5 ms d'attente et sa latence.
- **ASIO** : le SDK de Steinberg et sa licence, après la S26.
- **Le bend et la modulation enregistrés** : un ajout au domaine, à exposer.
- **Le choix d'une entrée MIDI par piste** : non demandé pour un débutant, hors des 26 semaines.
- **Les notes de la prise en SONG dans la toile** : le pattern n'existe qu'à la fin de la prise ; la ligne du
  transport les compte.
- **La refrappe d'une note tenue quand la tête est déplacée pendant la lecture** (§9.2).

## 12. Ce qui s'est mal passé, dit

- **`ae36568` a rendu la CI Windows rouge.** MSVC refusait l'`alignas(64)` des deux indices de la file (C4324,
  une structure remplie pour son alignement, en erreur sous `/WX`) ; clang ne connaît pas cet avertissement, et
  rien ici ne compile en MSVC. `7e4e8f3`, le correctif (les indices sans `alignas`), a donc été poussé sur une CI
  rouge, seul, et attendu au vert. Les six commits suivants, prêts en local, ont été rejoués dessus et poussés un
  par un, chacun après le vert du précédent.
- **Les trois tests des entrées MIDI n'avaient pas été cassés** quand `fa69eec` a été écrit, et son message dit le
  contraire. Ils l'ont été le 6 octobre, après coup : les trois sont tombés au rouge (§10).
- **Rien n'a été lancé sur Windows** : ni `scripts/build.cmd`, ni `--verify` avec copilote, ni
  `--verify-lecture` sur ta carte, ni Raw Input. Le chemin Raw Input n'est que compilé (par la CI Windows).
- **Le premier commit a été poussé sans relire l'avertissement de compilation qu'il montrait.** C'était un
  avertissement antérieur (`percent` inutilisé dans `DirectionPanel.cpp`, dans `daw_app`, hors `-Werror`), mais
  je l'ai vu après le push. Depuis, je relis avant de pousser.
- **Des avertissements antérieurs restent** dans `daw_app` et le binaire des tests du moteur, qui compilent JUCE
  et ne sont pas en `-Werror` : `-Wfloat-equal`, `-Wshadow`, une variable inutilisée, `-Wsign-conversion` dans
  `PluginPersistenceTests.cpp`. Aucun n'est de cette semaine ; je ne les ai pas corrigés, c'est un autre sujet.
- **Le passage cadencé de `--verify-jeu` s'est bloqué deux fois** à la fermeture de la carte : c'était mon
  montage (le tuyau de la sortie cadencée), pas le logiciel.
- **La reproduction de la lecture muette** a pris du temps pour un résultat que l'environnement ne permet pas de
  trancher (§7).
- **Un `pkill` a tué ma propre commande** une fois ; sans conséquence sur le dépôt.

## 13. Les écarts à l'acquis, dits

- **La logique pure du jeu vit dans `core/domain/live`**, avec la CI : file sans verrou, routeur, ligne de temps,
  clavier, prise, entrées. Le domaine n'avait de temps réel que `dsp`. Rien n'y lie JUCE ni Tracktion.
- **Un plugin interne de plus, posé par la projection** (`LiveInputPlugin`), en tête de chaque piste qui joue.
- **Le clic du métronome est une propriété de l'Edit qui ne vient pas du projet** (`clickTrackEnabled`, posé par
  `TakeRecorder`). C'est un réglage de l'écran, comme le mode du clavier.
- **Le plugin du jeu retient certains messages de Tracktion** (les coupures des clips pendant qu'une note jouée
  est tenue) et **refrappe des attaques** au départ et à l'arrêt.
- **Tracktion cherche les entrées MIDI chaque seconde** au lieu de quatre.
- **Un code propre à Windows** (`RawKeyboard.cpp`), sans équivalent sous Linux : là, le mode ne s'allume pas.
- **Deux options neuves** : `--verify-jeu` (une vérification) et `--sans-jeu` (le graphe de la S22, pour comparer
  `--verify-lecture`).
- **La règle du dernier 1/32 de temps** dans une prise en boucle : une décision de comportement prise en cours de
  semaine, pour réparer ce que la vérification a trouvé, sans te l'exposer avant. Elle est petite et se défait
  d'une constante (`wrapBeats`). Exposée après coup, **gardée telle quelle le 6 octobre 2026**.
- **Ctrl+T et Ctrl+R** sont pris ; ils étaient libres.

## 14. À essayer, dans l'ordre

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

## 15. Reste à faire

- **`--verify-lecture` avec et sans le jeu, sur ta machine** (§7).
- **La fenêtre « Audio »**, à la S24 (§11).
- **Le `--verify` complet avec copilote**, toujours pas relancé depuis la S22.
- **La piste compagne**, toujours sur sa branche.
- **4OSC au-delà de 32 voix** (§9.3).
- **`direction.amount` ignoré par les décalages de couleur des règles du mixage** (§2.4).
