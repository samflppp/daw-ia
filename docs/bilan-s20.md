# Bilan de fin de S20 — DAW IA

**Période :** semaine 20 sur 26. Rédigé le 3 octobre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`, push direct. 29 commits S20, de `9820417` à `0e84aa5`, plus ce bilan ; et 5 commits d'une autre session Claude (§7).
**Volume S20 (hors ce bilan, avec les 5 autres) :** 75 fichiers, +8 330 lignes, −160.
**Tests :** 430 cas ctest hors label `audio` (+30 depuis la S19), 51 cas Python (+6).
**CI :** verte sur `9db0a13` (Linux Clang et Windows) ; les commits suivants sont poussés, leur CI tourne à l'écriture de ce bilan.
**Vérifications de l'application (Release) :** `--verify-mix`, 71 sur 71, sans clé ; `--verify` complet sans copilote, 621 passées, 35 en échec, aucune étape neuve en échec (§8).
**Registry :** 60 types (+1, `track.set_role`). **Schéma du projet :** 6 (+1). **Enveloppe :** v3, inchangée.

## En bref

- **Le mixage par l'IA existe, de bout en bout.** Dans le mixer, « Mixer » mesure le morceau en local, décide
  (le modèle si le copilote est là, des règles sinon), vérifie ce qui est décidé contre des bornes écrites en
  code, essaie la proposition à blanc sur une copie du morceau, puis la montre tranche par tranche, chaque
  réglage avec sa phrase et la mesure qu'elle cite. Tu écoutes avant/après à niveau égal, tu décoches une
  tranche, tu bouges un axe, tu gardes ou tu refuses. Rien n'est écrit avant « Garder » ; « Garder » fait une
  seule entrée d'historique, par le copilote, défaite d'un Ctrl+Z à l'octet.
- **La référence est faite cette semaine**, comme tu l'as demandé : « Référence… » mesure un fichier comme le
  master, et le mixage va vers lui, de la part que tu choisis (§6).
- **« Mixe le morceau » au copilote** lance le même mixage (outil `mix.start`), avec un axe si tu le dis.
- **`--verify-mix` passe entièrement sans clé d'API** (71 vérifications sur 71). Les nombres des signaux connus sont ceux
  du calcul à 0,01 près.
- **En deçà :** l'avant/après sur tes trois morceaux et le coût réel attendent toi et ta clé (§4, §5). La mesure
  de 16 pistes de 3 minutes tient sous 20 s machine au repos (15 à 19 s), pas sur une machine chargée (§3).

## 1. Chantier 0

| Point | Ce qui a été fait | Preuve |
|---|---|---|
| L'automation du master (étapes 127–131, 144) | Pas un bogue : une cascade. L'étape 125 pressait Ctrl+Z sans que le copilote ait écrit (pas de clé), défaisait la ligne d'avant, et les suivantes en héritaient. L'étape ne presse Ctrl+Z que si le copilote a écrit, et le dit (`f38fa7a`). | Les six étapes passent sans copilote. |
| La ligne de génération lue en retard | Une aide `prompt()` tape, presse Entrée, puis attend que la lecture soit finie avant de rendre la main (`749f966`), aux sept endroits. La cause de la variante intermittente n'est **pas trouvée**. | Sept étapes S16–S19 passent. |
| Le tableau des échecs par cause | Ci-dessous (§8). | — |
| Mes vérifications et ta disposition | `--layout <fichier>` donne à une vérification sa propre disposition de fenêtres (`9820417`) ; ton `%APPDATA%\DAW IA\DAW IA.layout` n'est plus jamais réécrit, ni même remis. | Empreinte du fichier identique avant et après chaque passage. |

## 2. Le modèle retenu

Exposé en début de semaine et validé (« je valide tout et je te laisse faire le choix que tu recommandes pour
tout sauf pour l'option mixer avec une référence qui devrait être créée aussi »).

**Mesurer n'est pas décider.**
- **Mesurer** est du code local et déterministe (`domain/mix/Measurement`) : intensité BS.1770-4 (intégrée,
  court terme, momentanée), crête vraie suréchantillonnée 4×, dix octaves de 31,5 Hz à 16 kHz, facteur de
  crête sur 10 ms, corrélation et part du côté, activité, et le masquage entre pistes (même octave, au même
  moment, à 6 dB l'une de l'autre). Chaque piste est entendue après ses inserts, avant son fader ; le master
  à sa sortie. Le rendu se fait sur une copie de l'Edit, hors du fil des messages (`engine/MixRender`).
- **Décider** : le modèle ne reçoit que des nombres (le brief : rôles, chaînes, mesures, recouvrements, axes,
  cible, bornes), jamais un échantillon. Il rend une proposition par l'outil `mix.propose` : des cibles, pas
  des écarts (« Mixer » deux fois est idempotent), chacune avec une phrase et les mesures qu'elle cite.
- **Garder les bornes** : le code vérifie chaque réglage avant l'essai à blanc — volume de −12 à +6 dB, pan
  centré pour le kick, la basse et la voix, coupe-bas sous la plus basse note utile du rôle, gains d'égaliseur
  à ±6 dB, ratio ≤ 6:1, et surtout **la phrase doit citer une mesure qui existe, à 0,15 près, et dire ce
  nombre**. Le modèle voit une fois ce qui a été refusé et pourquoi ; ce qui est encore refusé est retiré.
- **Le modèle** : `claude-sonnet-5` (celui du copilote), effort `medium`, 12 000 jetons de sortie au plus,
  un tour de correction. Sans clé, ou si le modèle échoue, **le mixage de base par règles** décide, et l'état
  le dit.
- **Ce que le mixage touche** : volumes, pans, un égaliseur et un compresseur internes par piste, placés après
  tes plugins, réutilisés s'ils y sont déjà ; les envois. Jamais tes plugins (opaques, ni retirés ni
  contournés), pas de mastering, pas d'automation. Le master n'est corrigé que par son fader, et seulement
  s'il dépasse −1 dBTP à l'essai, avec sa phrase.
- **Les axes** : doux ↔ percutant, voix devant ↔ instru devant, serré ↔ large. Bouger un axe décide de nouveau
  sans mesurer de nouveau. (Pas de réverbération ni de délai internes : le troisième axe est « serré ↔ large ».)
- **Les rôles** : ce que tu as choisi (`track.set_role`), puis le nom de la piste, puis le son ; le brief dit
  d'où vient chaque rôle.

## 3. La mesure

**Signaux connus** (`--verify-mix`, chaque passage) :

| Signal | Attendu | Mesuré |
|---|---|---|
| Lead, sinus 1 kHz à −20 dBFS | −20,0 LUFS, −20,0 dBTP, −23,0 dB dans l'octave 1 kHz | −19,99 LUFS, −20,00 dBTP, −23,01 dB |
| Basse, sinus 60 Hz d'amplitude 0,5 | −9,0 dB dans l'octave 63 Hz | −9,09 dB |
| Le morceau | 20,000 s | 20,000 s |

Les tests du domaine couvrent en plus les cas 1 à 5 de l'EBU Tech 3341, la crête vraie d'un sinus à fs/4
décalé de 45°, le bruit blanc qui monte de 3 dB par octave, la largeur stéréo, la crête, l'activité, le
masquage et la somme de deux flux.

**Le temps.** Seize pistes de trois minutes (184 s), douze sur 4OSC ou douze canaux sampler, en fin de semaine, machine au repos (13 % de charge) : **15,4 à 18,9 s** sur six passages (copie de l'Edit sur le fil des messages : 0,62 à 0,67 s ; rendu et analyse : 14,8 à 18,2 s). En cours de semaine, les mêmes tests donnaient 27 à 28 s, et je t'avais dit que la cible tombait. **La cause de l'écart n'est pas démontrée** : la machine était plus chargée (d'autres sessions tournaient), et deux changements touchent le rendu depuis (la sonde ignore le pré-roll, la copie est reprojetée en mode chanson). La cible de moins de 20 s est tenue dans ces mesures, de peu ; elle ne l'est pas sur une machine occupée. La cible de moins de 20 s n'est **pas tenue** ; je te l'ai dit en cours de semaine.
Le rendu hors ligne de Tracktion prend à lui seul ~20 s ; deux pistes d'accélération ont été essayées (un fil
de calcul séparé, l'analyse en parallèle du rendu) et retirées, aucune ne gagnait. L'interface reste utilisable
pendant la mesure (lecture comprise), la mesure est reprise tant que rien de ce qui sonne n'a changé, et
« Annuler » rend la main en moins d'une milliseconde.

## 4. Avant / après sur tes trois morceaux

**Pas fait : il faut tes morceaux.** Ce que je te demande au §11 donne exactement ces nombres ; je remplirai
ce tableau avec ce que tu me rapportes (ou ce que montre `daw.log`, qui écrit chaque étape du mixage).

| Morceau | Master avant (LUFS / dBTP) | Master après | Kick/basse : marge du kick sur ses coups | Garde-fous franchis | À l'oreille, à niveau égal |
|---|---|---|---|---|---|
| 1 | | | | | |
| 2 | | | | | |
| 3 | | | | | |

Sur les signaux de `--verify-mix` : master −7,94 LUFS / +1,75 dBTP avant, −14,21 LUFS / −2,65 dBTP après
l'essai ; aucun garde-fou franchi ; le kick passe la basse à 63 Hz de 1,3 dB avant, 9,5 dB après, sur ses
coups ; entendus au même niveau à 0,02 dB près (RMS de ce qui est sorti par la carte son).

**Le recouvrement kick/basse, tel que mesuré, ne baisse pas sur ces signaux** (40 % → 40 %). Le critère (deux
octaves à 6 dB l'une de l'autre au même moment) ne voit pas le niveau d'un son qui décroît : un kick traverse
à chaque coup le niveau de la basse, quel que soit ce niveau. Ce que la phrase annonce (« pour laisser passer
le kick ») est vérifié autrement : la marge du kick sur la basse, sur le cinquième des instants où le kick est
le plus fort. Le critère de succès « le recouvrement baisse » reste à juger sur tes morceaux ; c'est une dette.

## 5. Le coût

**Pas mesuré : aucun appel d'API n'a été fait cette semaine**, ni par la CI ni par mes vérifications (une
vérification mixe toujours par les règles). Estimation, à confirmer au premier essai avec ta clé :
- **Lu par le modèle :** la consigne et l'outil, ~3 500 caractères ; le brief, ~500 caractères par piste
  (2 347 pour les 4 pistes de `--verify-mix`). Pour 16 pistes, ~12 000 caractères, soit **~4 000 à 5 000 jetons**
  (le tokenizer de Sonnet 5 compte ~3 caractères par jeton sur ce JSON français).
- **Écrit :** ~280 caractères par réglage (1 386 pour 5) ; 16 pistes, ~30 réglages, ~3 000 jetons, plus la
  réflexion à l'effort `medium`, ~2 000 à 4 000 : **~5 000 à 7 000 jetons**.
- **Prix** de `claude-sonnet-5` : 2 $ par million de jetons lus, 10 $ par million écrits.
- **Un mixage : ~0,06 à 0,08 $, soit ~0,06 à 0,07 €** ; le double au pire si le second tour (les refus) est
  nécessaire. Chaque mixage écrit ses jetons réels dans `daw.log` : le premier essai avec ta clé donne le vrai
  nombre.

## 6. La référence, et où elle se branche

« Référence… » dans la proposition, ou `MixHost::setReference(chemin)` :
1. le fichier est lu et mesuré hors du fil des messages par le même `StreamAnalyser` que le master ;
2. `mix::targetOf` en fait une **cible** : la pente des octaves autour de 1 kHz, la crête, la largeur ;
3. la cible entre dans le brief (`target`, `source` = le nom du fichier) : les règles et le modèle la lisent
   comme le reste, et `mix::towards` penche les axes vers elle, de la part choisie (0 à 1) ;
4. le reste ne change pas : mêmes bornes, même essai à blanc, même écoute.

C'est une cible de **forme** (pente, dynamique, largeur), pas d'intensité : on ne copie pas le niveau d'un
master fini. Un `Target` vide, c'est le mixage par les axes seuls.

## 7. Les écarts à l'acquis, dits

- **Le domaine reçoit les effets internes** : `PluginRef` au format `internal`, `daw.eq` et `daw.compressor`,
  paramètres en unités du domaine avec leurs bornes (`InternalEffects`), sans blob. C'était une des quatre
  décisions du début de semaine.
- **Un verbe ajouté** : `track.set_role` (registry 60), avec `MixRole` et le schéma 6 (le rôle n'est écrit que
  s'il est choisi).
- **`Reach::mix`** : les commandes du mixer disent qu'elles ne touchent que les tranches.
- **La référence est faite en S20**, pas en S21 comme prévu au départ : à ta demande.
- **Les effets internes passent aussi sur la piste compagne** (celle des enregistrements d'une piste qui joue
  des notes) ; **les plugins d'un canal sampler passent après le sampler** (ils passaient avant, et ne
  traitaient rien). Deux corrections de projection trouvées en écrivant les tests au rendu.
- **La couche de décision est arrivée en un commit** (`1354a2e` : rôles, garde-fous, mixage de base, axes,
  référence), parce que ces cinq parties se testent ensemble sur la même session.
- **La mesure rend toujours le morceau en mode chanson**, sur une copie de l'état, même quand la session joue
  un pattern (`9f64a6e`) : la première `--verify-mix` mesurait un morceau vide.
- **Le masquage se juge à travers les faders** (`0b2e699`) : les mesures du brief restent d'avant fader.
- **Une vérification ne mixe jamais par le modèle**, même avec une clé et le copilote lancés.
- **Une fenêtre d'infobulles** dans la fenêtre principale : elle porte les phrases de l'historique, et rend
  visibles les quelques infobulles qui existaient déjà (« Agrandir », « Fermer »…) et ne s'affichaient pas.
- **Cinq commits d'une autre session Claude** sur `main` cette semaine (`IDEES.md` et la règle sur l'audio
  généré, `549431a`) : je les ai trouvés en poussant et je me suis rebasé dessus, sans conflit.

## 8. Le `--verify` complet, échecs par cause

Sans copilote (pas de clé), dossier jetable, disposition jetable. Trois passages en fin de semaine :

| Passage | Passées | En échec |
|---|---|---|
| 1 | 617 | 40 |
| 2 | 619 | 37 |
| 3 (après la correction de l'étape du mixer) | **621** | **35** |

Les 35 du dernier passage, par cause — **aucune n'est neuve** :

| Cause | Étapes | Échecs |
|---|---|---|
| Le copilote absent (pas de clé, `--no-copilot`) et ce qui en dépend | 14–17, 61–62, 125–126, 176–177 | 26 |
| Trois étapes S18 de la toile hors de l'écran sur ce long projet | 183–185 | 6 |
| Le crescendo de vélocité : 63 au premier coup, pas 60 | 96 | 1 |
| « sombre » est devenu un mot de retouche, l'étape S16 attend « ignoré » | 150 | 1 |
| La phrase de la retouche S15 | 171 | 1 |

Ce que les deux premiers passages avaient de plus :
- **L'étape du mixer (72)**, deux fois : elle attendait au moins un réglage ; sur ce projet, les règles n'en
  proposent aucun, à raison (kick déjà à sa cible, deux pistes muettes dans le morceau, un clap joué une fois).
  `daw.log` l'a montré ; l'étape est corrigée (`0e84aa5`).
- **Les vu-mètres (58, 60)**, une fois sur trois : la lecture d'une boucle n'a rien fait entendre, toutes les
  pistes et le master à −100 dBFS, le moteur en lecture. **Intermittent, non diagnostiqué** ; c'est la forme de
  la dette S12 (« pas d'effet pendant la lecture »), qui ne s'était pas présentée depuis la S13.

Avec le copilote lancé, en début de semaine : 632 passées, 26 en échec, dont 17 étapes de pages et de fenêtres
qui ne passent que sans copilote. Reproduit deux fois, **cause non trouvée** (CLAUDE.md §6).

## 9. Ce qui s'est mal passé, dit

- **La mesure entendait le pré-roll du rendu** : Tracktion fait tourner ~0,5 s de blocs avant 0 pour installer
  les plugins. 6,5 s mesurées pour 6 s, et une intensité plus basse de 0,12 LU que celle du fichier rendu.
  Trouvé par le test qui relit le fichier ; la sonde n'entend plus que [0, fin) (`cd87418`).
- **La première `--verify-mix` a mesuré du silence** : le beatmaker s'ouvre en mode pattern, l'Edit y dure 0 s
  (`9f64a6e`).
- **Annuler pendant la mesure bloquait l'application** : le fil des messages attendait le rendu, qui attendait
  le fil des messages. Plus aucune attente d'un rendu en cours sur le fil des messages (`bcc5867`).
- **« pour une kick »** : les phrases collaient « une » devant chaque rôle. Vu sur l'écran (`b486049`).
- **Une compilation a donné des panneaux tous « à venir »** : après le changement de `PanelServices`, un
  binaire montrait le panneau d'attente partout ; un rebâtissage complet des fichiers qui l'incluent l'a fait
  disparaître. **Cause non trouvée** (une dépendance d'en-tête manquée par la compilation incrémentale, sans
  doute, mais je ne l'ai pas prouvé).
- **Un message de commit en avance sur sa preuve** : `5819d7d` dit que l'étape du mixer de `--verify` existe ;
  elle n'était pas encore passée au moment du commit, et elle a échoué deux fois avant d'être corrigée (§8).
- **Le mixage par les règles ne propose rien sur le projet de `--verify`** : juste, mais je l'ai d'abord pris pour
  un bogue. Ce sont les nouvelles lignes de `daw.log` (chaque piste du brief) qui ont tranché.

## 10. La règle du test cassé une fois

Chaque test neuf passé du premier coup a été cassé une fois, et tous sont passés au rouge :
- les effets internes au rendu (égaliseur et compresseur, sur les deux chemins de la piste) ;
- la mesure (EBU 3341, crête vraie, bandes, crête, activité, masquage, somme), le rendu du mixage, le mode
  pattern, le fichier rendu remis, `gained` et le brief à travers les faders, les articles des phrases, la
  ligne d'historique qui garde son contexte ;
- côté Python : `mix.decide` (refus, second tour, échec) et `mix.start` (routage cassé : rouge) ;
- `--verify-mix` : la marge du kick sur ses coups, en comparant l'avant à lui-même : rouge.
Ceux qui n'ont pas passé du premier coup : le crête du compresseur (le crête d'échantillon *monte* sous
compression : redéfini sur 10 ms), le creusement kick/basse (il choisissait l'octave 31,5 Hz : trié par
niveau), la durée mesurée (6,5 s au lieu de 6), l'intensité du fichier relu (0,12 LU d'écart : le pré-roll).

## 11. À écouter, dans l'ordre

Sur `main`, Release et Debug recompilés dans ton dossier de build. Ferme les autres DAW IA. Ta clé dans
`DAW_IA_ANTHROPIC_API_KEY` pour que le modèle décide ; sans clé, ce sont les règles, et l'état le dit.

1. **Ton premier morceau.** Ouvre-le, F10, « Mixer ». Regarde la progression : tu peux jouer le morceau pendant.
2. **Lis la proposition** : une ligne par tranche, « garder » allumé, la phrase de chaque réglage. Une phrase qui
   te semble fausse, note-la : elle cite une mesure, je peux la vérifier.
3. **« Avant », puis « Après »**, au même endroit du morceau : les deux sont au même niveau, tu juges le mix,
   pas le volume. Bascule plusieurs fois sur le refrain.
4. **Décoche une tranche** dont tu ne veux pas le réglage, puis « Garder ». Une seule entrée « Mixage par l'IA »
   dans l'historique ; survole-la : les phrases. **Ctrl+Z** : tout revient.
5. **Les axes** : « Mixer » de nouveau (la mesure est reprise, c'est immédiat), pousse « percutant », relâche :
   une autre proposition. Écoute.
6. **La référence** : « Référence… », un morceau fini que tu aimes, la part à moitié, « Mixer ».
7. **Au copilote** : « mixe le morceau, plus large ».
8. **Tes deuxième et troisième morceaux**, 1 à 3. Puis donne-moi, pour chacun : mieux / pareil / moins bien à
   niveau égal, et ce que tu as décoché. `daw.log` garde les mesures avant/après de chaque essai.

## 12. Reste à faire

- Tes trois morceaux et leurs nombres (§4), le coût réel (§5).
- La mesure sous 20 s sur une machine occupée (§3), et l'intermittent des vu-mètres (§8).
- Les dettes reportées de la S19 (CLAUDE.md §6) : le chantier 2, la cible de 8 ms de la toile, le glissé d'un
  point d'automation, la fermeture ; le piano-roll attend toujours ton essai, je n'y ai pas touché.
