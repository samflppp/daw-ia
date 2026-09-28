# Bilan de fin de S16 — DAW IA

**Période :** semaine 16 sur 26. Rédigé le 28 septembre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `claude/eloquent-mayer-pvkidy`, 9 commits de `da0acfb` à `dbba3a8`, plus
ce bilan. La PR [#1](https://github.com/samflppp/daw-ia/pull/1) a été fermée sans fusion : `main` ne contient pas
encore la S16.
**Volume depuis le bilan S15 :** 44 fichiers, +4 424 lignes, −195 (hors ce bilan).
**Tests :** 349 cas ctest (domaine, persistance, interface ; +22), 87 cas d'engine (dont 3 d'écoute au rendu),
42 cas Python (+9).
**CI :** verte sur le dernier commit (`dbba3a8`), Linux et Windows, lancée à la main (`workflow_dispatch`) puisque
la branche n'est pas `main`.
**Registry :** 55 types, inchangé. Ni l'écoute ni les retouches n'ajoutent de commande.

## En bref

- **Le geste est refait.** La zone d'abord, le prompt ensuite. Un bouton « ✦ Générer » et une pastille au bout de
  la plage : le geste se voit. La fenêtre se pose sous le piano-roll, jamais sur les notes, et ne propose rien
  avant qu'on ait écrit.
- **Une seule langue.** Le prompt passe par le modèle du copilote, avec les mêmes règles de traduction. Le lecteur
  local devient le repli, et la fenêtre le dit.
- **La ligne technique quitte l'écran.** Une phrase en mots de musicien la remplace ; la ligne reste sous
  « Détails ».
- **On écoute avant d'écrire.** « ▶ Écouter » joue les notes grises en boucle, par l'instrument de la piste, sans
  entrer dans le projet ni dans l'historique. Prouvé au rendu.
- **Des notes dans la zone sont retouchées,** pas écrasées : huit transformations, mesurées sur les notes avant et
  après.
- **Ce qui est tombé :** la zone sur plusieurs pistes (§6), et avec elle la playlist façon FL que tu as demandée
  en cours de semaine (§7).

## 1. Le modèle retenu

Exposé et validé le 27 septembre avant la première ligne. Tes réponses ce jour-là :
- **La playlist :** elle a une ligne par pattern, pas par piste ; tu as choisi de la refaire comme celle de FL,
  où un pattern passe d'une ligne à l'autre. Non commencé (§7).
- **`generation/Transform.h` :** accepté. C'est le seul ajout au moteur de génération.
- **La branche :** laissée à mon choix. J'ai poussé sur la branche de la session, pas sur `main`.

| Point | Ce qui a été construit |
|---|---|
| La zone | Maj + glisser sur la règle, sinon la plage des notes choisies, sinon le pattern entier. |
| Le point d'entrée | « ✦ Générer » dans l'en-tête du piano-roll, pastille au bout de la plage, Ctrl+G. |
| La fenêtre | Une bande sous les vélocités : prompt, Écouter, Valider, Fermer ; une ligne d'état ; « Détails » replié. La grille rétrécit, elle n'est jamais recouverte. |
| Avant le prompt | Rien. Entrée sur un prompt vide : « Écris d'abord ce que tu veux entendre. » |
| La lecture | `generation.interpret` dans le process du copilote ; « Je lis ta demande… » bouge pendant l'attente ; au-delà de 8 s, en échec ou sans copilote, le lecteur local répond et la fenêtre le dit. |
| La phrase | Construite en local depuis ce que le moteur a résolu, pas depuis ce que le modèle affirme : elle reste vraie hors ligne. |
| Les variantes | Flèches ◀ ▶ et compteur « 2 / 5 », Alt + molette. |
| Valider | Un groupe `generator`, une entrée d'historique. |

**Ce que le copilote reçoit pour lire un prompt :** le texte, la longueur de la zone, la mesure, la présence de
notes, le nom des pistes. Jamais l'état du projet, jamais ce qui a été appris : le §3 de la S15 tient.

## 2. La phrase en mots de musicien

`generation::phrase` : la longueur, le rôle et la tonalité toujours ; le reste seulement s'il a été demandé ; la
forme sans ses lettres ; « dans ton style » à partir d'un quart appris.

| Cas | Phrase |
|---|---|
| Rien d'imposé, deux mesures | « deux mesures de mélodie en la mineur » |
| Après 32 croches de la personne | « quatre mesures de mélodie en la mineur, dans ton style » |
| « Am doubles grave aéré boucle » | « … en la mineur, en doubles-croches, dans le grave, aérée, en boucle » |
| Fa# mineur, accords | « deux mesures d'accords en fa dièse mineur » |
| Une retouche | « même rythme, notes plus sombres » |

La S15 demandait de montrer la forme dans la ligne. La S16 la retire de l'écran par défaut : elle est dans
« Détails » pour qui veut comprendre.

## 3. Une seule langue

- **L'outil `prompt.read`** reprend le schéma de `pattern.generate` tel que le DAW le sert, sans les champs de
  placement. Un champ ajouté au contrat côté C++ arrive au lecteur sans changer une ligne Python.
- **Les règles de traduction** (« triste » donne mineur, « calme » donne sparse, « trap » donne des
  doubles-croches…) sont communes au copilote et à la fenêtre.
- **Réponse courte et effort bas :** 1 024 jetons au plus, `effort: low`, sur le modèle configuré du copilote.
  Changer de modèle pour aller plus vite est ton choix ; c'est noté dans `IDEES.md`.
- **Preuves (CI, fournisseur scripté) :**
  - le schéma est bien celui de `pattern.generate` sans placement ;
  - la réponse revient au contrat S14 ;
  - un appel coupé, un modèle qui ne fait que parler ou une clé absente donnent un échec en français ;
  - le schéma n'est demandé au DAW qu'une fois.
- **Le repli** (`RoutedPromptReader`, testé sans process ni horloge) :
  - copilote absent : réponse locale immédiate ;
  - copilote en échec : réponse locale ;
  - trop lent : réponse locale, et la réponse tardive est jetée ;
  - un second prompt remplace le premier, un prompt annulé ne répond jamais.

**Pas prouvé ici :** la lecture par le vrai modèle. Il n'y a pas de clé d'API dans l'environnement de
construction. C'est à toi de l'essayer (§9).

## 4. Écouter avant d'écrire

**Le chemin**, exposé avant d'être écrit :
- `ProjectProjector::listen` pose les clips de la ligne avec les notes de la plage remplacées par la proposition,
  exactement ce que Tab écrirait. Une ligne que la proposition ouvrirait reçoit des clips à part, qui partent
  avec l'écoute.
- Le transport boucle sur la plage. Il est piloté directement, sans commande `transport.*` : `ProjectState` n'est
  jamais muté.
- Rien de neuf dans le callback audio. Les clips sont réécrits sur le fil des messages, comme toute édition de
  note depuis la S3, et Tracktion reconstruit son graphe hors du thread audio.
- Toute commande de transport termine l'écoute d'abord. L'export l'arrête : un export est le projet.

**C'est un changement du moteur audio**, pas du moteur de génération. Tu avais demandé de le dire : il était
exposé dans le modèle, et c'est le seul endroit où la semaine touche `core/engine`.

**La preuve au rendu** (`GenerationRenderTests`, 3 cas) :

| Mesure | Avant | Pendant l'écoute | Après |
|---|---|---|---|
| Attaques entendues (doubles-croches) | 0, 32 | 0, 16, 24 : la note hors plage gardée, les deux proposées, pas la note remplacée | 0, 32 |
| Hauteurs aux temps 4 et 6 | — | sol, la, comme proposé | — |
| Projet sérialisé | référence | identique à l'octet | identique à l'octet |
| Historique | référence | inchangé | inchangé |

Une autre variante pendant l'écoute s'entend aussitôt. Une ligne neuve s'entend puis disparaît. Une commande
`transport.stop` termine l'écoute, et on entend de nouveau le projet.

**Ce que le premier passage a trouvé :** rien. Il était vert du premier coup, et c'est suspect après trois verts
qui ne regardaient rien depuis la S13. J'ai donc cassé `listen()` exprès, pour qu'il n'applique plus la
proposition : les trois cas sont passés au rouge. Ils regardent bien le rendu.

## 5. Les retouches

Quand la zone contient des notes, Ctrl+G les reprend. Le prompt dit comment ; sans consigne, le rythme est gardé.
`generation/Transform.h` est une couche à côté du générateur : Harmony, la forme, le style et le contrat sont
utilisés tels quels. `TransformProposal` tient la retouche à côté de `GhostProposal`, qui n'est pas modifié.

**Ce que chaque consigne change dans les notes, et la preuve** (`TransformTests`, mélodie de 14 notes en la mineur
sur Am | F | G | Am, seuils écrits avant le premier passage) :

| Consigne | Ce qu'elle change | Mesuré |
|---|---|---|
| garder le rythme | Les hauteurs : le générateur tire la zone, ses hauteurs vont dans l'ordre sur les attaques d'origine. | attaques, durées et vélocités identiques ; au moins la moitié des hauteurs changées ; tout en la mineur ; 4 variantes |
| garder les hauteurs | Le rythme : un rythme tiré, avec autant d'attaques que de notes. | suite des hauteurs identique ; attaques différentes ; tout dans la zone |
| variante légère | Un seul changement, comme un A′ : une note déplacée d'un pas, une vélocité de ±15, ou la fin fermée sur la tonique ou ouverte sur la quinte. | une note partie, une note venue, sur 12 variantes ; les trois sortes tirées |
| plus sombre | Même rythme ; en majeur, les degrés 3, 6 et 7 baissent d'un demi-ton ; en mineur, chaque note descend d'un degré. | rythme identique ; hauteur moyenne plus basse ; do-mi-sol-la-si-do en do majeur donne do-mib-sol-lab-sib-do |
| plus clair | L'inverse. | rythme identique ; plus haut ; tout en la majeur |
| plus rythmé | Chaque note assez longue est jouée deux fois. | plus d'attaques ; la suite des hauteurs, doublons retirés, identique |
| plus calme | Les notes hors du temps partent, la précédente est tenue. | moins de notes ; une sous-suite de la suite d'origine ; tout sur les temps |
| humaniser | Les placements bougent de 1/64 de temps au plus, les vélocités de 8 au plus. | hauteurs et ordre identiques ; décalages sous le seuil ; au moins la moitié des attaques bougent |

**Justesse.** Ce que le générateur écrit reste dans la tonalité. Une note que la personne a écrite hors de la
tonalité n'est pas touchée par les consignes qui gardent les hauteurs : les règles décident de ce qu'écrit le
générateur, pas de ce qu'a fait la personne.

**La lecture.**
- En local, par les mots : « garde le rythme », « plus sombre », « humanise »…
- À distance, par un champ `transform` de `prompt.read`, transmis seulement quand la zone contient des notes.
- Les mots employés pour la consigne ne sont plus dits « ignorés ».

**Ce que le premier passage a trouvé.**
- **Deux noms de test contenaient un point-virgule.** ctest coupe le nom à cet endroit, et un cas coupé peut
  passer sans rien exécuter : c'est le piège que `check_hygiene.py` connaît. Corrigés, puis vérifiés : chaque cas
  exécute ses assertions (de 6 à 128).
- **Un test de la lecture locale exigeait qu'aucun mot ne soit ignoré dans « plus sombre en croches ».** Il était
  rouge, à juste titre sur le fond mais pas sur ce qu'il devait regarder : « en » est un mot vide que les mots de
  la S14 signalent. Le test vérifie maintenant ce qu'il doit, que « plus » et « sombre » ne sont plus ignorés.

## 6. Vérifications

Les vérifications de l'application ont tourné sous Linux (Xvfb), sans carte son et sans copilote. Les étapes S14
et S15 adaptées au nouveau geste passent.

| Étape S16 | Résultat ici |
|---|---|
| Ctrl+G ouvre la fenêtre sous la grille et sous les vélocités ; rien n'est proposé ; Entrée sur un prompt vide ne propose rien ; le projet et l'historique n'ont pas bougé | OK |
| « propose-moi quelque chose » : des notes grises en la mineur ; la phrase « deux mesures de mélodie en la mineur » ; ni « (déduit) », ni « AABA », ni « variante » dans la phrase ; « Détails » replié ; les détails gardent ce que le moteur a choisi | OK |
| Après 32 croches de la personne, la phrase dit « dans ton style » | OK |
| ▶ Écouter : le vu-mètre du Lead sonne | **ÉCHEC ici, attendu :** sans carte son le moteur ne joue pas (−100 dBFS). Le bouton, le projet, l'historique, la variante suivie et l'arrêt par Ctrl+Espace sont OK. |
| Retouches : « garde le rythme », « plus sombre », « humanise », Tab, Ctrl+Z | **Pas encore passées** dans l'application : ajoutées après la dernière exécution. Leur effet est prouvé par `TransformTests`. |

**Deux rouges qui ne regardaient pas le bon objet.** « Échap ferme » et « Tab ferme » étaient rouges sur un produit
juste : le contrôle lisait la visibilité du champ, qui garde son propre drapeau quand la fenêtre qui le contient
se cache. Il lit maintenant la fenêtre (`59a52cd`). C'est la quatrième fois depuis la S13, et la deuxième dans le
sens rouge.

Les autres échecs du passage complet viennent de l'environnement et existaient avant la semaine : pas de carte son,
pas de copilote, export MP3/AAC réservé à Windows.

## 7. Ce qui est tombé, et ce qui reste en deçà

**Tombé :**
- **La zone sur plusieurs pistes (§6 de la commande).** Elle suppose la playlist façon FL que tu as tranchée le 27 :
  des lignes libres, un pattern qui passe de l'une à l'autre. Cela ajoute un champ de ligne au placement, donc
  touche le format du projet et le journal. Non commencé ; c'est le premier chantier que je propose pour la S17,
  avec un modèle à valider avant.

**En deçà :**
- **L'écoute en SONG** boucle sur le premier placement du pattern, pas sur celui qu'on regarde.
- **Retoucher des notes choisies une à une :** la zone est la plage des notes choisies, et la retouche reprend
  toutes les notes de cette plage.
- **Une seule consigne par prompt :** « plus sombre et plus calme » ne prend que la première reconnue.
- **Les variantes se parcourent une à une,** avec un compteur. Pas encore côte à côte.

Tout cela est noté dans `IDEES.md`, pas construit.

**Un commit cassé.** `fa2fa82` ne compilait pas sous MSVC : `std::back_inserter` sans `<iterator>`, que clang
tolérait. C'est le cas inverse du `<cmath>` de la S15, et c'est contraire à la règle « aucun commit cassé ». Tu l'as
vu en compilant chez toi, avant que je lise la CI ; `dbba3a8` le corrige, et la CI Windows est verte depuis.

## 8. Les écarts à l'acquis, dits

- **`core/engine` change,** pour l'écoute (§4). C'était exposé dans le modèle et accepté.
- **Le format du projet ne change pas.** La playlist façon FL le changera, et c'est la raison pour laquelle elle
  n'a pas été glissée en fin de semaine.
- **La ligne technique n'est plus à l'écran par défaut,** à rebours de la S15 : c'est ta décision du 27.

## 9. À essayer et à écouter, dans l'ordre

Sur `claude/eloquent-mayer-pvkidy`, compilée chez toi, clé d'API réglée.

1. **Le geste.** Piano-roll, un canal Lead vide, Maj + glisser sur deux mesures de la règle, clic sur la pastille
   « ✦ Générer ». La fenêtre s'ouvre vide, sous les vélocités. Est-ce le bon endroit ?
2. **La langue.** « des accords tristes », Entrée. Le compteur « Je lis ta demande… » tourne, puis les notes
   grises et la phrase arrivent. Note le temps de lecture : je ne l'ai pas mesuré avec le vrai modèle.
3. **L'écoute.** « ▶ Écouter » : la boucle doit sonner sur l'instrument du canal, sans entrée dans l'historique.
   Change de variante pendant l'écoute : on doit entendre la nouvelle aussitôt, sans trou. C'est le test le plus
   important de la semaine.
4. **Une retouche.** Valide (Tab), garde la zone, Ctrl+G, « plus sombre », Écouter, puis « humanise », Écouter.
   Dis-moi si « plus sombre » sonne plus sombre ou seulement plus bas.
5. **Le repli.** Coupe le copilote et refais l'étape 2 : la fenêtre doit dire « Hors ligne ».
6. **Tes crédits d'API** sont bas (2,46 $). À l'épuisement, la fenêtre passera au lecteur simple.

## 10. Reste à faire

- Fusionner la branche dans `main` (la PR #1 est fermée ; la rouvrir suffit).
- **S17 :** la playlist façon FL et la zone multi-pistes, sur un modèle validé avant d'être codé.
- Ton écoute (§9), et les vérifications de l'application sur ta machine, avec carte son.
- Le bug intermittent : il ne s'est pas présenté cette semaine.
