# Bilan de fin de S14 — DAW IA

**Période :** semaine 14 sur 26. Rédigé le 26 septembre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`, 7 commits de `8828876` à `3bb5246`, plus ce bilan.
**Volume depuis le bilan S13 :** 38 fichiers, +6 103 lignes, −13 (hors ce bilan).
**Tests :** 310 cas ctest (domaine, persistance, interface ; +24), 84 cas d'engine (+2, rendus), 30 cas Python (+11).
**Vérifications par l'application :** 425 dans la liste, aucune en échec (§6).
**Registry :** 55 types, inchangé. La génération n'ajoute aucune commande : Tab écrit des `note.add` et des
`note.remove` dans un groupe marqué `generator`.

## En bref

- **La génération locale est livrée** : règles pour la justesse, Markov pour le style, notes fantômes au
  piano-roll. Tout est local, sans API, et prend environ 1 ms par proposition, pour un budget de 16 ms.
- **Le corpus n'était pas là.** Le pipeline est écrit et testé ; ce qui joue aujourd'hui est le repli écrit à
  la main (§3).
- **Mon avis à la lecture des notes, pas à l'oreille :** c'est juste, et ça erre. Le défaut est dans le moteur,
  pas dans la zone de saisie (§7). C'est ce qu'il faut écouter en premier.

## 1. Le modèle, tel que validé

Le modèle a été validé le 26 septembre avant d'être codé. Les cinq points ouverts ont été tranchés ainsi :

| Point | Décision |
|---|---|
| Où tourne la génération | Elle tourne en C++, dans `core/domain/generation/` : du code pur, sous les gardes S1, qui marche sans process Python. Le Python (`services/harmony`) ne fait plus que le hors-ligne : lire le corpus et compter. Le squelette S1 plaçait le moteur harmonique dans `services/` ; c'est l'écart assumé. |
| Transposition sur douze tons | Elle n'est pas écrite. Les comptes sont en degrés et en intervalles depuis la tonique ; un test le montre sur la même phrase en Am et en F#m. |
| Tab et les notes déjà là | L'acceptation remplace les notes de la ligne qui commencent dans la plage. |
| Choisir la plage | Maj + glisser sur la règle. Sans plage, c'est le pattern entier. |
| Où va le corpus | Hors du dépôt : `%USERPROFILE%\DAW IA corpus\raw\`, et le modèle dans `%APPDATA%\DAW IA\generation\`. |

**Justesse (`Harmony`).** Les règles énumèrent les hauteurs légales : la gamme (majeur ou mineur naturel), le
registre du rôle, la grille de la résolution, la plage, et pour les accords les triades de la gamme. La tonalité
est déduite par Krumhansl-Kessler ; à égalité à 0,05 près, une relative mineure l'emporte sur sa majeure.
Am F C G se lit sinon en Do. Chaque accord de mesure est lu dans les autres lignes.

**Style (Markov, `StyleModel`).** Deux chaînes d'ordre 3, avec un repli lissé jusqu'à l'ordre 0 :
- le rythme : l'écart jusqu'à l'attaque suivante, sachant les trois derniers écarts et la position dans la
  mesure ;
- la hauteur : le pas en degrés, sachant les trois derniers pas.

S'y ajoutent une table des degrés (temps fort ou faible), une table des durées, une table des vélocités par
double-croche, et pour les accords une chaîne des fondamentales. Le Markov ne voit que des candidats légaux : il
ne peut pas écrire une fausse note.

**Variantes.** La variante k est un tirage avec la graine hash(contexte, contraintes, k), par un générateur écrit
à la main (splitmix64) : les mêmes entrées donnent les mêmes notes sur toute machine. Les doublons sont sautés.

**Note fantôme.** `GhostNote {pitch, velocity, startBeats, lengthBeats}`, sans identifiant, tenue par un
`GhostProposal` dans le panneau. Avant Tab, il n'y a aucune commande, aucune entrée, aucun identifiant tiré.
La proposition garde une copie de son contexte et en compare le hash après chaque changement du projet :
- un Ctrl+Z qui touche les notes autour la régénère ;
- un changement ailleurs la laisse en l'état ;
- un pattern ou une ligne qui disparaît la ferme.

## 2. Le contrat S14 ↔ S15 : les contraintes

`generation::Constraints` (`Constraints.h`) comporte cinq champs optionnels : `key`, `resolution`, `density`,
`register`, `role`. Un champ vide veut dire « à déduire du contexte ». `Interpretation` y ajoute les mots
ignorés et les conflits. Forme JSON :

```
{"key":{"tonic":"A","mode":"minor"},"resolution":"1/16","density":"dense",
 "register":"low","role":"bass","ignored":["sombre"],"conflicts":[]}
```

`ConstraintInterpreter::interpret(text, done)` est asynchrone dans sa forme. `LocalInterpreter` en est le seul
implémenteur ; il rappelle tout de suite. En S15, l'interprète distant rendra ce même JSON ; ni la zone ni le
générateur ne changent. Aucun appel réseau n'a été écrit.

Mots compris :

| Champ | Mots |
|---|---|
| Tonalité | Am, F#m, Bbm, C… |
| Résolution | noires, croches, doubles, doubles-croches, 1/4, 1/8, 1/16 |
| Densité | clair, moyen, dense |
| Registre | grave, medium, aigu |
| Rôle | mélodie, basse, accords, rythme, et quelques synonymes : lead, 808, drums… |

Un autre mot est ignoré, et la zone le dit. Deux mots pour un même champ : le dernier gagne, et la zone le dit
aussi.

## 3. Le corpus : ce qu'il a apporté

**Rien, cette semaine : il n'était pas sur la machine.** Tout ce qui sonne vient du **repli**, écrit à la main
dans `StyleModel::fallback()` :
- pas conjoints favorisés, et retour par pas après un saut ;
- tonique, tierce et quinte sur les temps forts ;
- attaques qui retombent sur le temps ;
- une fondamentale de basse qui reste sur I, V et VI ;
- une progression i → VI → III/VII.

Il est juste et plat, comme prévu. C'est aussi le modèle de la CI.

**Ce qu'il me faut, et où :**
- **Format :** des `.mid` exportés de FL, de type 0 ou 1, une boucle ou un pattern par fichier, une piste par
  canal avec son nom (« 808 », « Lead », « Keys »…).
- **Emplacement :** `%USERPROFILE%\DAW IA corpus\raw\`. Les sous-dossiers sont lus.
- **Recommandé :** `%USERPROFILE%\DAW IA corpus\corpus.csv`, séparé par des points-virgules :
  `fichier;bpm;tonalité;piste=rôle;…`, par exemple `Nuit.mid;142;F#m;Lead=melody;808=bass`. Il bat
  l'estimation : les exports FL n'ont souvent pas de tonalité.
- **Volume :** sous environ 50 boucles par rôle, l'ordre 3 recopie des phrases entières. Vers 200, ça porte.

**Lancer :** `uv run daw-services corpus build` depuis `services/`. La commande écrit
`%APPDATA%\DAW IA\generation\markov.json` et `markov-rapport.txt`. Le rapport donne :
- les fichiers refusés et pourquoi (illisible, doublon d'octets ou de notes, piste jouée hors grille) ;
- les pistes et les événements par rôle ;
- les tonalités, lues ou détectées ;
- la part de notes hors de la gamme détectée.

Le DAW charge le modèle au premier Ctrl+G, et `daw.log` dit lequel : `generation: style model corpus (…)` ou
`… repli (…)`.

**Un chiffre à lire en premier dans le rapport : la part de notes hors gamme.** Deux causes possibles :
- **Si elle dépasse environ 10 %**, la drill et le trap que tu fais sont probablement en mineur harmonique (le
  sol# en la mineur). La règle actuelle le refuse, et ces notes ne sont pas comptées. Il faudrait alors
  ajouter ce mode : c'est une règle de justesse, pas du style.
- **Sinon,** c'est la détection de tonalité qui se trompe, et `corpus.csv` la corrige.

**Le contrat Python ↔ C++.** Les règles (degrés, tonalité, accords) sont écrites deux fois et épinglées par
les mêmes cas des deux côtés. `services/tests/fixtures/style-small.json` est écrit par le pipeline à partir
d'un corpus de test ; `GenerationTests.cpp` le relit et génère avec.

## 4. L'écran

- **Choisir une plage :** Maj + glisser sur la règle du piano-roll. La plage est teintée ; Échap l'efface.
- **Ctrl+G :** la zone s'ouvre en haut de la grille, au-dessus du début de la plage, et **propose tout de
  suite** avec ce qu'elle contient. Une zone vide propose donc aussi, avec les contraintes déduites.
- **Sous la zone,** une ligne montre la proposition, par exemple : « La mineur (imposé) · doubles (imposé) ·
  dense (imposé) · grave (imposé) · basse (imposé) · variante 1/1 · style : repli · ignoré : sombre ».
- **Entrée :** régénère avec le texte saisi. Sur le même texte, Entrée passe à la variante suivante :
  redemander la même chose donnerait sinon les mêmes notes.
- **Alt + molette :** variante suivante ou précédente, jusqu'à 16.
- **Tab :** écrit un groupe, marqué `generator`, avec le libellé « génération : Am doubles basse ». Si la
  ligne n'existait pas, elle est ouverte dans ce même groupe.
- **Échap :** ferme la zone. Rien n'a été écrit.
- **Les notes que Tab remplacerait** sont estompées sous le gris.

## 5. La preuve au rendu (test d'engine, `GenerationRenderTests.cpp`)

Le test accepte une proposition comme Tab l'accepte, la fait jouer par 4OSC, la rend hors ligne, puis
écoute :

- **Attaques :** par double-croche, soit un niveau qui monte, soit une classe de hauteur qui change. 4OSC tient
  son niveau d'une note legato à la suivante : sans le second critère, la moitié des attaques passaient
  inaperçues. Le test exige deux choses :
  - aucune attaque là où aucune note ne commence ;
  - chaque attaque audible entendue. Seule exception, une note liée à la précédente sur la même classe de
    hauteur, qui ne produit ni l'un ni l'autre.
- **Hauteurs :** YIN (`engine/PitchDetection.h`), sur chaque note. **Une hauteur hors de la gamme fait échouer
  le test.** La classe doit être exacte, l'octave juste à une près : sous 100 Hz, YIN peut prendre le double de
  la période de l'onde de 4OSC, ce qui change l'octave sans changer la classe.

Résultats :
- **Mélodie Am, trois variantes :** 18, 17 et 12 notes ; toutes les hauteurs mesurées et exactes ; aucune
  attaque manquée, aucune en trop.
- **Basse F#m en croches, deux variantes :** 12 notes chacune ; toutes les classes exactes.

## 6. Vérifications (`VerificationGeneration.cpp`)

425 vérifications, aucune en échec. Les 12 étapes S14 passent par le clavier et la souris, sur un canal
« Lead » sans sample.

| Étape | Mesure |
|---|---|
| Maj + glisser sur la règle | la plage va des temps 4 à 12 ; rien n'est écrit |
| Ctrl+G, zone vide | des notes grises, dans la gamme et dans la plage, en 0,9 ms ; le projet est identique à l'octet près ; aucune entrée ; aucune tonalité lue dans les kicks |
| « Am doubles dense grave basse sombre », Entrée | La mineur imposé, une basse, dans son registre grave ; « ignoré : sombre » |
| Entrée sur le même texte, Alt + molette | variante 2, d'autres notes ; retour à la variante 1, les mêmes notes qu'avant |
| **Désordre :** Échap, puis Ctrl+G deux fois | une seule proposition ; rien d'écrit |
| **Désordre :** un fader bougé, puis Ctrl+Z | la proposition reste, avec les mêmes notes ; rien d'écrit |
| **Désordre :** changer de pattern | la proposition se ferme ; rien d'écrit |
| **Désordre :** pendant la lecture, « Am croches mélodie » | la lecture continue ; toutes les attaques sur des croches |
| Tab, toujours pendant la lecture | une entrée marquée générateur ; ce qui est écrit est exactement ce qui était gris ; la lecture continue |
| Au rendu, le Lead seul | chaque attaque audible à sa place, aucune en trop ; chaque hauteur mesurée, dans la gamme, à la classe écrite |
| Ctrl+Z, puis tout défaire | le projet d'avant Tab, puis le projet d'origine, à l'octet près |

**Ce que le premier passage a trouvé.** Il comptait 423 vérifications, toutes vertes, mais deux choses étaient
fausses dans le rapport lui-même :

1. **Zone vide, « Do mineur (déduit) ».** Les lignes Kick et Hat jouent sur 4OSC, une seule hauteur chacune, et
   comptaient comme de l'harmonie. Une ligne qui ne joue qu'une hauteur ne compte plus. Un test du domaine
   l'épingle, et l'étape vérifie désormais qu'aucune tonalité n'est lue dans des lignes de batterie.
2. **« ignoré : mÃ©lodie ».** Le littéral de l'étape passait par `juce::String(const char*)`, qui lit le
   Latin-1. Le produit n'était pas en cause : une frappe réelle est en UTF-8. Mais l'étape ne vérifiait pas le
   rôle. Elle passe maintenant par `fromUTF8` et vérifie que rien n'est ignoré.

C'est le piège du §6 de la S13 sous une autre forme : un vert qui ne regarde pas ce qu'il croit regarder.

## 7. Ce qu'aucun test ne dit : mon avis, à la lecture des notes

Je ne peux pas écouter. J'ai lu, note par note, des propositions du repli sur un pattern vide :

- **Mélodie Am :** `A4 A4 A4 E5 D5 C5 A4 | E4 F4 C5 B4 G4 A4 D5 C5 B4`. Tout est juste, le phrasé est
  plausible et les temps forts tombent sur la tonique. **Mais aucun motif ne revient.** Une boucle de beat vit de
  la répétition : une mesure, puis la même avec une variation. Un Markov d'ordre 3 ne se souvient pas de ce
  qu'il a joué quatre temps plus tôt. Je prévois une marche aléatoire dans la gamme, écoutable mais pas une
  topline.
- **Basse Am :** `A2 G2 D2 G1 E1 E1 A1 A1 A1 F1 A1`. Elle se promène tant qu'aucune ligne d'accords ne lui dit
  où aller. Sur une vraie grille, elle suit mieux, parce que les notes de l'accord pèsent double sur les temps
  forts.
- **Accords :** ils sont justes, mais commencent parfois sur iv ou III, et leur rythme est irrégulier (attaques
  aux temps 1, 3 et 4, à cheval sur le temps).

**Verdict, avant ton écoute : correct et plat, c'est-à-dire le cas prévu par la séparation.** Ce n'est pas la
zone de saisie qu'il faut reprendre. Si c'est inécoutable, voici l'ordre que je propose ; je n'ai rien fait de
tout cela, puisqu'il fallait d'abord ton oreille :

1. **La forme avant le Markov :** générer une mesure, puis la répéter en variant la dernière moitié (AA′ ou
   AAAB). C'est une règle de structure, peu coûteuse, et elle fera plus pour « sonne comme une boucle » que
   n'importe quel corpus.
2. **Le corpus lui-même.** Il remplacera les préférences écrites à la main par les tiennes, mais il n'ajoutera
   pas la répétition.
3. **Le repli déjà prévu :** une génération rythmique seule (rôle `rythme`, qui existe déjà : hauteur du canal,
   positions et vélocités). Sur un canal sample, c'est ce que fait déjà Ctrl+G.

## 8. Ce qui est tombé, et pourquoi

- **Le mineur harmonique et les autres modes.** Ils ne sont pas dans la règle, et c'est un choix de périmètre.
  Le rapport du corpus dira s'ils manquent (§3).
- **La tonalité par plage.** Elle est déduite sur tout le pattern, pas mesure par mesure. Une modulation dans un
  pattern n'est pas vue.
- **Une attaque ailleurs que sur le début de la plage.** La première note tombe toujours sur le premier pas. Une
  mélodie qui entre en levée ne peut pas encore être proposée.
- **Le contexte de provenance.** Le groupe du générateur ne porte pas de digest de contexte ; celui du copilote
  n'en porte pas non plus aujourd'hui. Les contraintes sont dans le libellé.
- **La densité déduite.** Elle ne l'est pas : elle reste « moyen » par défaut.

## 9. À écouter, dans l'ordre

Sur un projet où le pattern 1 contient une ligne d'accords (Keys, 4OSC ou ton instrument) :

1. **Un canal Lead vide, Maj + glisser sur les mesures 1 à 2, Ctrl+G.** Écoute la proposition de la zone vide :
   sélectionne la plage, Tab, lecture. C'est le cas le plus sévère pour le moteur, puisque rien ne lui dit quoi
   faire.
2. **Même plage, « doubles dense », Entrée, puis Alt + molette cinq fois.** Les variantes se distinguent-elles
   à l'oreille, ou seulement à l'œil ?
3. **Un canal 808, « basse grave » sur quatre mesures,** sous la ligne d'accords. La basse doit suivre les
   fondamentales. Si elle ne le fait pas, c'est un défaut de règle, et je le veux signalé.
4. **« accords » sur un canal vide.** La progression et son rythme. Je m'attends à ce que ce soit le plus
   faible des trois (§7).
5. **Un canal sample (hat), Ctrl+G, zone vide, « dense » puis « clair ».** C'est la génération rythmique seule,
   le repli. Si 1 à 4 ne passent pas, c'est ce que tu entends là qui décide si le repli suffit à un beat rap.
6. **Ctrl+G pendant la lecture, puis Tab.** La lecture ne doit pas broncher.
7. **Quand le corpus sera là :** `uv run daw-services corpus build`, puis lire le rapport (part hors gamme,
   pistes refusées), relancer le DAW et refaire 1 à 3. La différence avec le repli est ce qui décide de la
   ligne « à trancher fin S14 » d'`IDEES.md`.

## 10. Reste à faire

- Ton écoute (§9), puis la décision du §7 : la forme, ou le repli rythmique.
- Le corpus : le déposer et lancer le pipeline. Préalable juridique hors du dépôt (`IDEES.md`).
- Le bug intermittent : il ne s'est pas présenté pendant les deux passages de la semaine.
- Le fader d'une tranche automatisée (dette de la S13, au moment du polish).
