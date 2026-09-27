# Bilan de fin de S15 — DAW IA

**Période :** semaine 15 sur 26. Rédigé le 27 septembre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`, 11 commits de `eb80d37` à `6021252`, plus ce bilan (réécrit après le chantier copilote, §10).
**Volume depuis le bilan S14 :** 40 fichiers, +4 540 lignes, −27 (hors ce bilan).
**Tests :** 327 cas ctest (domaine, persistance, interface ; +17), 85 cas d'engine (+1, rendu), 33 cas Python
(+3).
**Vérifications par l'application :** 484 dans la liste, aucune en échec (§6 et §10).
**Registry :** 55 types, inchangé. Ni la forme ni l'apprentissage n'ajoutent de commande.

## En bref

- **La forme est livrée.** Le générateur écrit une unité, puis la répète en variant : AABA, AAAB, AAB, AA′,
  boucle. Sur quatre mesures en AABA, les mesures 1, 2 et 4 ont le même rythme et la 3 un autre, dans 16
  variantes sur 16. Sans forme, la mesure 2 répète la 1 dans 1 variante sur 16.
- **Le générateur apprend de la personne qui l'utilise,** à partir de ce qu'elle écrit dans le DAW, et sur sa
  machine seulement. Deux projets de styles opposés donnent deux générateurs mesurablement différents (§4).
- **Le corpus personnel est retiré du périmètre.** Plus aucune demande de corpus ; `IDEES.md` est tranché sur
  ce point.
- **Le copilote écrit de la musique** (demandé en cours de semaine) : « ouvre un omnisphere et cree des accords
  triste dans un pattern en 140 bpm » passe, avec le vrai modèle (§10).
- **Rien n'est tombé** de ce qui était demandé. Ce qui reste en deçà est au §7.

## 1. Le modèle de structure, tel que validé

Validé le 27 septembre avant d'être codé.

**Où.** Une couche au-dessus du Markov, pas à sa place : `generation/Form.h` et une classe `Shaper` dans
`Generator.cpp`. Le Markov écrit une unité sur une plage réduite ; la forme la pose sur la plage. `Harmony`
n'est pas modifié : chaque transformation choisit ses hauteurs dans la liste des hauteurs légales.

**L'unité.** Une mesure. Deux pour une mélodie de huit mesures ou plus. Pour les accords, l'unité est la
rythmique de la première mesure ; la progression reste tirée mesure par mesure et revient après quatre.

**Le schéma par défaut**, selon le rôle et le nombre d'unités :

| Unités | Mélodie | Basse | Rythme, accords |
|---|---|---|---|
| 1 | libre | libre | libre |
| 2 | AA′ | AA′ | boucle |
| 3 | AAB | AAB | boucle |
| 4 et plus | AABA, en cycle | AAAB, en cycle | boucle |

**Les transformations.** Aucune ne régénère toute l'unité.
- **A :** l'unité telle quelle. Sur un autre accord, la basse suit la fondamentale (même contour, décalé du pas
  entre les deux fondamentales), la mélodie glisse ses temps forts à la note d'accord la plus proche. Le rythme
  ne bouge pas.
- **A′ :** un seul changement, tiré par la graine : une note déplacée d'un pas de la grille, une vélocité
  changée de 15, ou la dernière note fermée (tonique) ou ouverte (quinte, ou seconde pour une mélodie).
- **B :** la première moitié de l'unité gardée, la seconde retirée par le Markov, qui a la première moitié pour
  historique. Son rythme doit différer de celui de l'unité : quatre tirages au plus, puis une note déplacée de
  force. La fin est ouverte.
- **Le A qui ferme un AABA** garde son rythme et finit sur la tonique.

**La contrainte.** `Constraints` gagne un sixième champ optionnel, `form`. Le contrat S14 §2 reste valable : un
JSON sans `form` se lit toujours, et l'interprète distant de S16 devra seulement connaître ce champ en plus.

| Champ | Mots |
|---|---|
| Forme | AABA, AAAB, AAB, AA' (ou AA′), boucle, varié, libre |

« varié » fait de chaque retour un A′ différent. « libre » redonne la génération de la S14, pour comparer.

**La ligne d'information** dit la forme comme le reste : « La mineur (imposé) · … · mélodie (imposé) · AABA
(déduit) · variante 2/16 · style : repli ».

## 2. Ce que la forme a changé, à la lecture des notes

Je ne peux pas écouter. J'ai lu les propositions du repli sur un pattern vide, en La mineur :

- **Mélodie, S14 (« libre ») :** `E5 E4 D4 C4 C4 | D4 C4 C4 | E4 D4 D4 B4 G4 | A4 E4 G4`. Quatre mesures
  sans lien entre elles, avec des attaques qui dérivent.
- **Mélodie, AABA :** `F4 D4 A4 G4 | F4 D4 A4 G4 | F4 D4 A4 G4 E4 | F4 D4 A4 A4`. On reconnaît la mesure. La
  troisième repart pareil, puis part ailleurs sur sa seconde moitié et s'ouvre sur la quinte. La quatrième
  revient et se pose sur la tonique.
- **Basse sur Am | F | G | Am, AAAB :** `A2 A2 A1 | F2 F2 F1 | G2 G2 G1 | A2 A2 A2 E2`. Le même motif suit
  la grille, puis une fin qui relance.
- **Accords, boucle :** la même rythmique à chaque mesure. Le défaut du §7 de la S14 (des attaques aux temps 1,
  3 et 4, à cheval sur le temps) disparaît.

**Mon avis avant ton écoute :** c'est la différence entre une marche dans la gamme et une boucle. Deux
réserves :
- **le motif lui-même reste celui du repli.** La forme rend audible la répétition, elle ne rend pas le motif
  meilleur. C'est le rôle de l'apprentissage ;
- **une unité faible répétée quatre fois s'entend davantage** qu'une marche aléatoire. Une basse en rondes
  répétée (variante 2 de « F#m basse croches », §5) en est l'exemple. Alt + molette passe à la suivante.

## 3. Le modèle d'apprentissage, tel que validé

Validé le 27 septembre avant d'être codé.

| Point | Décision | Pourquoi |
|---|---|---|
| La source | Les notes des patterns de ses projets `.dawproj`, projet courant compris. Un MIDI importé devient des notes du projet, donc il compte. | C'est ce qu'elle produit, et c'est déjà dans le DAW. Un dossier MIDI à scanner serait un second chemin. |
| Ce qui ne compte pas | Les notes écrites par le générateur ou le copilote et jamais retouchées. Le journal et les reçus le disent (`origin`). | Sans cela, le modèle apprendrait ses propres propositions et tournerait en rond vers le repli. Une note retouchée compte : elle est à elle. |
| Le moment | Le projet courant, compté à chaque Ctrl+G. Les autres, comptés à chaque sauvegarde (l'autosave de 30 s comprise) sur un thread à part, à partir d'une copie de l'état. | La mesure écrite il y a 30 secondes pèse déjà sur la proposition suivante. Le fil des messages ne fait que copier ; le thread audio n'en entend jamais parler. |
| Où vit le modèle | Des comptes par projet, dans `%APPDATA%\DAW IA\generation\appris\<id>.json`, mêlés à la demande : repli + tous les autres projets (poids 1) + projet courant (poids 3). | La vitesse d'un modèle global et la cohérence d'un modèle par projet. Exclure ou oublier un projet revient à écarter un fichier. |
| Le démarrage à froid | Le repli S14, plus la forme, plus le projet courant dès sa première note. | La forme décide de la première impression. Des préréglages par genre, sans corpus, seraient écrits à la main : du repli déguisé. |
| Le mélange | Le repli pèse comme 200 notes. Là où la personne a joué, la part apprise vaut N / (N + 200) à chaque contexte ; ailleurs, le repli reste. Par rôle. | Pas de bascule : la part monte à chaque note écrite. La basse peut être apprise quand la mélodie est encore au repli. |
| Ce qu'elle voit et contrôle | La ligne : « style : 32 % appris (1 projet, 32 notes) · 68 % repli ». Fichier > Génération : apprendre de mes projets, apprendre de ce projet, oublier ce qui a été appris (confirmé). | La ligne dit d'où vient le style, comme elle disait « style : repli ». L'exclusion vit sur la machine, pas dans le projet : un projet partagé ne change pas le comportement chez l'autre. |

**L'interface `StyleModel`.** Ce n'est pas une interface : c'est une classe de valeurs, des tables de comptes.
Je l'ai dit avant de coder, et le modèle appris est une seconde **source** du même type, pas un second
implémenteur. `Generator` ne change pas d'une ligne pour l'apprentissage. `StyleModel` gagne `of()` et une
comparaison ; `GhostProposal` gagne une ouverture qui garde le modèle en vie (`shared_ptr`), puisque le style
est rebâti à chaque Ctrl+G.

**Le comptage est en C++.** Les règles de `corpus.py` sont écrites une seconde fois dans `Learning.cpp`, pour
apprendre sans process Python. Elles sont épinglées aux règles Python par le même fixture : `LearningTests`
reconstruit le corpus de `test_corpus.py` en deux patterns et retrouve, table par table et au compte près, les
tables de `style-small.json`. Le pipeline S14 garde son rôle de contrat ; il n'est plus l'exécutant.

### Tout reste local — un argument produit

Ce que le générateur apprend d'une personne ne quitte pas sa machine :
- les comptes sont écrits dans son dossier d'application, jamais dans un projet, jamais dans le journal ;
- le copilote ne les voit pas. Il reçoit l'état du projet, et les comptes n'en font pas partie ;
- aucune requête réseau n'est écrite pour cela ; la CI n'appelle aucune API ;
- tout oublier supprime les fichiers, et rien ne subsiste ailleurs.

C'est une contrainte technique, et c'est aussi ce qu'un producteur veut entendre avant d'écrire dans un outil
qui apprend : son style reste à lui. Un outil qui entraîne un modèle central sur ses utilisateurs ne peut pas
le dire.

Je n'ai pas écrit de test qui vérifie que le copilote ne reçoit pas les comptes. Un tel test ne regarderait
rien : les comptes ne sont pas dans `ProjectState`, et la vue du copilote ne lit que `ProjectState`. C'est le
piège du §6 de la S13 et de la S14, et je préfère le dire que l'écrire en vert.

## 4. La preuve de l'apprentissage (`LearningTests.cpp`)

Deux projets de quatre patterns de quatre mesures, en La mineur :
- **A :** croches, mouvement conjoint (0 1 2 3 4 3 2 1), 128 notes ;
- **B :** doubles-croches en sauts de quarte et de quinte, 256 notes.

Chacun est appris comme l'application apprend le projet ouvert (poids 3), puis mêlé au repli. Seize
propositions de quatre mesures sont tirées sur une ligne vide, sans résolution imposée. Les seuils étaient
écrits avant le premier passage.

| Mesure | Repli | A | B | Seuil |
|---|---|---|---|---|
| Écart moyen entre attaques (doubles) | 3,62 | 2,25 | 1,20 | A > B + 0,5 |
| Écarts de deux doubles (croches) | 35 % | **88 %** | 2 % | A ≥ 60 % |
| Écarts d'une double | 3 % | 2 % | **93 %** | B ≥ 60 % |
| Intervalle moyen (degrés) | 1,79 | 1,17 | 2,94 | B > A + 1 |
| Intervalles conjoints (0 ou 1 degré) | 51 % | **88 %** | 19 % | A ≥ 60 % |
| Sauts (3 degrés ou plus) | 23 % | 4 % | **74 %** | B ≥ 50 % |

Distance entre les histogrammes de A et de B, de 0 (identiques) à 1 (rien en commun) : **0,91** pour les
écarts, **0,70** pour les intervalles. Toutes les notes restent dans la gamme : l'apprentissage classe, les
règles décident.

**Ce que le premier passage a trouvé.** Il échouait sur ces mêmes seuils : croches à 53 %, doubles à 32 %. Le
repli pesait 200 notes à chaque contexte, même aux contextes fins, que la personne n'avait joués que 48 fois.
La part réelle y tombait vers 20 % quand la ligne en aurait affiché 66 %. Le repli pèse maintenant 200/N fois
ce que la personne a joué dans chaque contexte. La part apprise est la même partout, et c'est celle que la
ligne affiche. Le seuil n'a pas bougé ; le mélange, si.

**Pas de bascule.** La part de croches dans les propositions, selon le poids de ce qui a été appris du projet A
(part apprise entre parenthèses) :

| Poids | 0 | 0,25 (14 %) | 0,5 (24 %) | 1 (39 %) | 2 (56 %) | 4 (72 %) |
|---|---|---|---|---|---|---|
| Croches | 35 % | 45 % | 58 % | 73 % | 85 % | 91 % |

Elle monte à chaque pas, sans saut. Elle monte aussi plus vite que la part affichée : le générateur accentue
les préférences avant de tirer (température 0,75). La ligne dit donc le poids dans les comptes, pas la part de
ce qu'on entend.

**Les notes de la machine.** Un test écrit deux notes « générateur » et une note de la personne. Si la personne
déplace l'une des deux notes du générateur, seule l'autre reste exclue. Le journal d'un projet rouvert donne le
même résultat.

## 5. La preuve au rendu (test d'engine)

`GenerationRenderTests.cpp` gagne un cas. « Am mélodie AABA » est acceptée comme Tab l'accepte, jouée par
4OSC et rendue hors ligne. Le test compare les attaques entendues, mesure par mesure. Les mesures 1, 2 et 4
s'entendent au même rythme, la 3 à un autre, sur trois variantes. Les hauteurs sont toutes mesurées et toutes
dans la gamme, comme en S14.

Les propositions S14 du même test sont maintenant en forme par défaut, puisque la plage fait quatre mesures.
Elles passent toutes.

## 6. Vérifications

474 vérifications, aucune en échec. Dix étapes S15.

**La forme** (`VerificationForm.cpp`), sur un canal Lead neuf et quatre mesures :

| Étape | Mesure |
|---|---|
| Ctrl+G | la ligne dit « AABA (déduit) » ; motifs des notes grises {0 4 8 10 12 14} ×2, {0 4 8}, {0 4 8 10 12 14} ; rien d'écrit |
| « boucle », puis « libre » | les quatre mesures au même rythme ; « libre (imposé) » ; rien d'ignoré |
| « Am AABA mélodie », Tab | ce qui est écrit a la forme annoncée ({0 2 4 10}, {0 2 4 10}, {0 2 4 8 12}, {0 2 4 10}) ; Ctrl+Z, puis tout défaire à l'octet près |

**L'apprentissage** (`VerificationLearning.cpp`). Il se fait dans le dossier de la vérification, jamais dans
celui de la personne : une vérification n'apprend rien au générateur de la machine et ne lit rien de ce qu'il a
appris.

| Étape | Mesure |
|---|---|
| « Lead A » vide, Ctrl+G | « style : repli » ; aucun fichier appris |
| Tab, puis Ctrl+G | les notes du générateur n'apprennent rien : « style : repli » |
| 32 croches écrites par la personne sur « Lead B » | « appris (1 projet, 32 notes) » ; style bâti en 7,7 ms ; croches dans les propositions : 47 % au repli, 73 % après |
| Enregistrer | un fichier, celui du projet, compté hors du fil des messages : 32 notes, aucune du générateur |
| Fichier > Génération | « repli (projet exclu) », le fichier ne garde que l'exclusion ; « apprentissage coupé » ; retour |
| Un autre projet appris | « appris (2 projets, 64 notes) » |
| Oublier | plus aucun fichier ; « appris : oublié » ; le projet ouvert apprend encore ; tout défaire |

**Ce que le premier passage a trouvé.** Le premier passage de la forme (446 vérifications, toutes vertes)
notait « zone vide » à l'étape Ctrl+G. C'était faux : la zone rouvre avec le texte précédent (« Am croches
mélodie »), et la ligne le montrait. La mesure elle-même était juste, mais le libellé ne décrivait pas ce qui
était vérifié. Il dit maintenant « proposé ». C'est le même piège qu'aux §6 de la S13 et de la S14, en plus
petit : une vérification verte doit dire ce qu'elle regarde vraiment.

**Un chiffre que j'avais mal lu.** La première version de ce bilan disait « style rebâti en 7,7 ms ». Le log
du même passage montrait quatre reconstructions entre 14,3 et 17,4 ms, dont deux au-dessus des 16 ms, quand un
Ctrl+G en suivait un autre sans que rien ait changé. Le comptage du projet ouvert est maintenant gardé tant que
le bus ne dit pas qu'il a changé (`f125d15`). Dernier passage : 0,56 ms à l'étape mesurée. Tout est en Debug.
Sur un gros projet, le premier Ctrl+G après une modification reste le cas coûteux, et il n'est pas mesuré.

## 7. Ce qui est tombé, et ce qui reste en deçà

Rien de ce qui était demandé n'est tombé. En deçà :

- **Les autres projets sont relus au lancement seulement** (et après « oublier »). Un projet enregistré dans une
  autre instance n'est vu qu'au lancement suivant. Comme un projet ouvert remplace le processus (Fichier >
  Ouvrir relance l'application), le cas ne se présente pas aujourd'hui.
- **Le menu n'a pas d'écran à lui.** Trois lignes dans Fichier > Génération. Il n'y a pas de liste des projets
  appris, ni de moyen d'exclure un projet fermé.
- **Le mineur harmonique** n'est toujours pas dans les règles. Un utilisateur qui écrit des sol# en la mineur
  les verra ignorés par le comptage des degrés, comme en S14.
- **La forme ne décide pas où la mélodie entre.** La première note tombe toujours sur le premier pas de
  l'unité, donc pas de levée (déjà dit en S14 §8).
- **Le poids du projet courant (3) et celui du repli (200)** sont des choix, pas des mesures. Ils seront à
  revoir sur de vrais projets.

## 8. À écouter, dans l'ordre

1. **La forme, sur un canal Lead vide : Maj + glisser sur quatre mesures, Ctrl+G, Tab, lecture.** Tu dois
   entendre une mesure, la même, une autre, la première. Puis la même plage avec « libre » : c'est la S14. La
   différence entre les deux est ce que la semaine a apporté.
2. **Alt + molette sur la proposition 1, cinq fois.** Une variante est une autre unité, donc une autre boucle.
   Repère celles dont l'unité est faible : une unité faible répétée s'entend plus qu'un défaut noyé.
3. **« basse » sur quatre mesures sous une ligne d'accords.** Le motif doit suivre les fondamentales et relancer
   en mesure 4 (AAAB). Si un changement d'accord sonne faux, c'est la règle « suivre la fondamentale » qu'il
   faut revoir, et je le veux signalé.
4. **« accords » sur un canal vide.** La même rythmique à chaque mesure : c'était le plus faible des trois en
   S14.
5. **L'apprentissage :** écris toi-même huit mesures de mélodie dans un style net (par exemple des croches
   conjointes), puis Ctrl+G sur un autre canal. La ligne dit la part apprise. Compare avec Fichier >
   Génération > Apprendre de ce projet décoché : c'est le repli.
6. **Un hat, Ctrl+G, « boucle » puis « varié ».** Le rôle rythmique apprend aussi de tes lignes de batterie.

## 9. Reste à faire

- Ton écoute (§8), en commençant par la forme.
- L'interprète distant des contraintes (S16) : en partie avancé par le §10. Le copilote écrit déjà le JSON du
  contrat S14 §2 pour pattern.generate ; reste la zone de saisie, qui ne passe toujours que par LocalInterpreter.
- Le bug intermittent : il ne s'est pas présenté pendant les passages de la semaine.
- Le fader d'une tranche automatisée (dette de la S13, au moment du polish).

## 10. Le copilote écrit de la musique (demandé le 27 septembre)

**Le problème.** « ouvre un omnisphere et cree des accords triste dans un pattern en 140 bpm » échouait sur
« note.add: missing key clipId ». Le copilote écrivait chaque note des accords par un note.add. La réponse du
modèle, plafonnée à 2048 jetons, se coupait au milieu du dernier appel, et l'agent ne regardait pas pourquoi elle
s'était arrêtée : l'appel vide partait avec le groupe, qui était refusé en entier.

**Ce qui a changé.**
- **`pattern.generate`, un outil musical pour le copilote.** Une ligne, une plage, et les contraintes du contrat
  S14 §2 plus `form`. Le DAW le déroule avec son propre générateur (règles, forme, style appris de la personne)
  en note.remove et note.add, exactement comme Tab. Le registry reste à 55 types. Le modèle ne peut pas écrire
  une fausse note, puisque ce ne sont plus ses notes.
- **L'essai à blanc.** Avant d'appliquer, et après chaque tour du modèle, le DAW rejoue ce qui attend sur une
  copie de l'état. Chaque étape y voit ce que les précédentes ont créé : la ligne ouverte deux appels plus tôt
  existe quand pattern.generate écrit dedans. Un appel refusé est retiré, et le modèle apprend pourquoi dans le
  résultat de cet appel, pour le corriger dans la même requête.
- **La limite.** 8192 jetons, et un appel coupé n'est plus jamais mis en attente : le modèle est invité à le
  renvoyer.
- **Le prompt** dit quand préférer pattern.generate à note.add, et comment traduire une intention :
  « triste » donne du mineur, « calme » une densité sparse. « triste » n'est pas un axe du générateur ; la
  réponse dit ce que le modèle a choisi.

**Les preuves.**
- `CopilotRequestTests` (CI, sans API) : la demande mise en commandes donne Omnisphere sur une piste neuve,
  140 BPM, quatre triades de la mineur, une seule entrée copilote, et un Ctrl+Z qui rend le projet à l'octet
  près. Un appel refusé est nommé par son rang, et le projet ne bouge pas.
- `test_copilot.py` (CI, faux modèle) : un appel coupé n'est pas pris, un appel refusé revient au modèle et
  seule sa correction est appliquée, et la demande Omnisphere part en un groupe qui finit par pattern.generate.
- **La vérification, avec le vrai modèle et la phrase mot pour mot** (deux passages) :

| Mesure | Résultat |
|---|---|
| Réponse | « J'ai créé une piste Omnisphere avec le plugin chargé, un nouveau pattern de 4 mesures placé à la suite (mesure 37, temps 144), avec un changement de tempo à 140 BPM à cet endroit. J'y ai généré des accords tristes en la mineur, densité clairsemée, en boucle. » |
| Historique | une entrée, marquée copilote |
| Omnisphere | chargé sur une piste neuve |
| Accords | 12 notes, 4 attaques, chacune une triade de la mineur, toutes les notes dans la tonalité annoncée |
| Ctrl+Z | le projet d'avant, à l'octet près |

**Ce que le premier passage a trouvé.** Il échouait sur « une tonalité mineure : Do majeur ». Le produit avait
bien écrit la mineur, et le disait. Mon contrôle déduisait la tonalité des notes, et des triades de la mineur
sont les notes de do majeur : seule la tonique les distingue. Le contrôle lit maintenant la tonalité annoncée
par la réponse, puis vérifie que chaque note et chaque triade y appartiennent. Le troisième vert qui ne regardait
pas ce qu'il croyait regarder, cette fois dans l'autre sens : un rouge qui ne regardait pas le bon objet.

**Un commit cassé.** `45c9b8d` ne compilait pas sous Linux : `std::llround` sans `<cmath>`, que MSVC tolérait
par un include transitif. La CI l'a vu, `6021252` le corrige. C'est contraire à la règle « aucun commit cassé ».

**Deux choses à savoir.**
- **Le modèle interprète.** Au premier passage il a réglé le tempo du projet à 140. Au second, il a posé un
  changement de tempo à 140 au début du nouveau pattern, laissant le reste du morceau à son tempo. Les deux
  lisent « un pattern en 140 bpm ». Si tu veux l'un plutôt que l'autre, c'est une règle de prompt.
- **Chaque essai à blanc copie l'état,** une fois par tour du modèle (huit au plus). C'est négligeable devant
  les secondes d'un appel au modèle, et ce n'est pas mesuré sur un gros projet.

**À essayer :** la même phrase dans le panneau Copilote, puis écouter les quatre accords sur Omnisphere. Puis
une variante qui combine : « ajoute une basse 808 qui suit ces accords, en croches ».
