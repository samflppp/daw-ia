# DAW IA — reprise de contexte

Ce fichier sert à ce qu'une session Claude Code neuve reprenne le projet sans rien redemander.
Contexte produit et stratégique complet : `docs/contexte-projet.md`.

## 1. Le projet en cinq lignes

DAW assisté par IA : socle natif Tracktion Engine + JUCE (`core/`), services Python séparés
(`services/`, JSON-RPC sur socket local), workspaces déclaratifs (`workspaces/`). Un copilote pilote
mixage, routage et paramètres via le Command Bus ; un moteur génératif (à venir) injecte du MIDI et de
l'audio par le même bus. Windows est la seule cible produit. Projet mené en solo, en incubateur, vers un
comité en mars 2027, sur un plan de 26 semaines dont chaque semaine ferme sur un bilan dans `docs/`.

**Cap actuel.** Jalon go/no-go atteint en S8. S18–S19 : ergonomie (une seule toile playlist/piano-roll).
S20 : le mixage par l'IA — « Mixer » mesure le morceau en local, un modèle (ou des règles, sans clé)
décide à partir des seuls nombres, des garde-fous en code bornent, l'essai à blanc est rendu, la personne
écoute avant/après à niveau égal et garde ou refuse tranche par tranche ; un mixage « comme ce morceau »
vers une référence. S21 : la lecture muette fermée (la carte son
gardée ouverte), fenêtres qui se posent, rack et historique qui défilent, premier mixage réel par le modèle
(0,074 $) ; la piste compagne reste sur la branche `s21-piste-compagne`. S22 : un clip audio séparé en quatre
stems sur leurs pistes (HTDemucs, local) ; une direction tirée de références (tempo, tonalité, sections,
équilibre, couleur — des nombres), gardée dans le projet (`direction.set`, schéma 7), lue par le mixage, la
génération et le copilote, corrigeable au panneau Direction (F11). S23 : on joue l'instrument de la piste choisie au clavier MIDI et au
clavier de l'ordinateur (Ctrl+T, lu par la place des touches, Raw Input), sans rien écrire ; ● ou Ctrl+R enregistre
dans le pattern en cours, décompte et clic, en un groupe d'historique. S24 : la fenêtre « Audio » (F12), son conseil
mesuré sur la carte ; les effets posés, déplacés (`plugin.move`) et contournés depuis la tranche du mixer ; le flux audio
(F3), le routage réel en graphe calculé depuis `ProjectState`, le son à chaque endroit par des prises dans chaque
chaîne, l'avant et l'après d'un effet, les gestes du graphe, l'écoute seule d'un état, la proposition du mixage vue
dans le graphe ; le kit choisi par des règles dans les samples de la personne, mesurés une fois sur la machine
(page « Kit ») ; les bus intelligents, essayés à blanc et gardés en un groupe (page « Bus »). S25 : on dirige le DAW à la voix — Ctrl droit
tenu (ou « Parler »), le micro de l'ordinateur ouvert pendant l'appui seulement, le morceau baissé de 20 dB, Parakeet v3
en local transcrit, la phrase s'affiche dans le champ du copilote : sûre, elle part après 1,5 s qu'une touche retient ;
douteuse, elle attend Entrée et ne fait rien ; dite, elle est gardée comme telle dans le journal. Plan des six dernières semaines (`docs/plan-s21-s26.md`) :
S21 solidité (lecture muette d'abord) et fin de l'ergonomie ; S22 séparateur de stems et direction par
références ; S23 jouer au clavier MIDI et au clavier AZERTY ; S24 le kit et le mixer ; S25 le DAW à la voix ;
S26 le prototype emballé. Le piano-roll reste jusqu'à la phase d'essais, après la S26 ; le fine-tune pendant
l'incubation. Plus aucun verbe ajouté au domaine sans une raison de démonstration.

## 2. Décisions d'architecture acquises — ne jamais rouvrir

| Décision | Semaine | Pourquoi |
|---|---|---|
| Command Bus : toute mutation de `ProjectState` passe par une commande sérialisable | S1–S2 | annulation, versioning et pilotage MCP avec un seul mécanisme |
| Aucune commande n'engendre d'identifiant dans `apply()` | S2 | l'appelant engendre `clipId`/`noteId` avant l'exécution ; sinon rejouer le même payload donnerait un projet différent |
| `ProjectState` est la seule vérité, l'`Edit` Tracktion n'est qu'une projection, `UndoManager` de Tracktion jamais utilisé | S3 | deux sources de vérité est le piège classique ; tranché une fois pour toutes |
| Projection par réconciliation, liée par identité (`TrackId`/`ClipId` dans le `ValueTree`), jamais par position | S3 | lier par position casse dès qu'un élément est supprimé au milieu ; coût borné et payé volontairement |
| Règle de thread du bus : le bus retient le thread qui l'a construit et refuse les autres (`ErrorCode::wrongThread`, vérifié en release) | S3 | `core/domain` ne peut pas demander « suis-je sur le thread message », notion de framework ; `rebindToCurrentThread()` couvre la passation délibérée |
| Journal append-only distinct de `journal()` (vue de la pile d'undo) | S5 | `core/persistence` garde tout, y compris commandes annulées et annulations elles-mêmes — voir `docs/persistence.md` |
| Pattern / Placement / Clip / Lane séparés, liaison par identifiant jamais par rang | S9–S17 | `pattern.create`+`ownLane`, `pattern.place`, `Lane{LaneId,name}` dans `ProjectState` ; réordonner les lignes ne réécrit aucun bloc |
| Mesurer n'est pas décider : la mesure est du code local déterministe, la décision un modèle qui ne lit que des nombres (jamais d'audio), les garde-fous du code vérifiés avant l'essai à blanc | S20 | un réglage se justifie par une mesure vraie, citée dans sa phrase ; le modèle ne peut ni inventer un nombre ni franchir une borne |
| Effets internes (`PluginRef` format `internal`, `daw.eq`/`daw.compressor`, paramètres en unités du domaine) : seuls effets que le mixage par l'IA règle ; les plugins de la personne restent opaques, jamais retirés ni contournés | S20 | un plugin tiers est une boîte noire ; on ne règle que ce dont on connaît le sens |
| Le mixage gardé est un seul groupe d'historique par le copilote, sa mesure et ses phrases rangées par empreinte (`Provenance.context`) | S20 | un Ctrl+Z défait tout, à l'octet ; l'historique sait redire pourquoi |
| Une référence ne laisse dans le projet que des nombres (jamais son audio, ses notes ni son chemin) ; la direction vit dans `ProjectState`, un tempo ou une tonalité ne se moyenne jamais entre références | S22 | elle se rouvre et s'annule avec le projet ; un désaccord se dit au lieu d'être deviné |
| Le jeu en direct n'écrit rien : file sans verrou par piste (`domain::live`), plugin interne en tête de chaîne (`LiveInputPlugin`), placement régulier (un bloc + une marge) ; une prise devient des commandes existantes en un groupe, à la fin, identifiants de l'appelant, sans quantification ; au tour de boucle, un départ dans le dernier 1/32 de temps est écrit au début (`wrapBeats`, gardé le 6 octobre 2026) | S23 | le jeu ne passe pas par le bus ni n'attend le fil des messages ; un Ctrl+Z retire la prise à l'octet |
| Le clavier de l'ordinateur se lit par scan code (Raw Input, `RIDEV_INPUTSINK`, sur un fil à lui), disposition de FL ; avec Ctrl, Alt ou Windows, aucune touche ne joue | S23 | une lettre change de place entre AZERTY et QWERTY, la place non |
| Le flux audio est calculé depuis `ProjectState` (`flux::graphOf`), sa disposition et sa vue restent à l'écran ; le son s'y lit par des prises de la projection dans chaque chaîne (`FluxTapPlugin`), armées seulement quand leur nœud est à l'écran, jamais un verrou ni une allocation sur le fil audio | S24 | un graphe qui montre le vrai routage, jamais une illustration ; une prise posée seulement à l'ouverture rebâtirait le graphe du moteur |
| Le kit est choisi par des règles sur des mesures locales (`domain::kit`), sans modèle ; l'index des samples vit sur la machine, jamais dans le projet | S24 | les contraintes (808 accordée, grave libre, couleur commune) se mesurent et se vérifient ; décidé le 6 octobre 2026 |
| Le push-to-talk : Parakeet TDT 0.6B v3 en local dans un processus Python à lui (`daw-services voix`), le micro hors du moteur et ouvert seulement pendant l'appui, jamais celui d'un casque Bluetooth sans le dire ; Ctrl droit lu par sa place ; une phrase douteuse n'agit jamais, une sûre part après 1,5 s ; ce qui a été entendu dans `Provenance.context`, jamais le son | S25 | décidé le 7 octobre 2026 ; une phrase mal comprise ne déclenche rien, le journal distingue dit et tapé sans changer l'enveloppe |
| Windows seule cible | S1 (annoncé), acté S5 | support Linux/Ubuntu reporté post-MVP ; `setup-ubuntu.sh` reste pour la CI, jamais lancé en cible produit |

Rouvrir une de ces lignes veut dire que quelque chose de nouveau la contredit réellement — c'est couvert
par la règle de méthode « dire quand un choix contredit l'acquis » (§4), pas une réévaluation de confort.

## 3. Invariants vérifiés automatiquement

Trois règles d'hygiène, vérifiées en CI :

1. **Aucune valeur visuelle en dur** (couleurs, espacements, tailles, rayons, police) — tout vient de
   `core/ui/tokens/tokens.json` via `daw::ui::Tokens`.
2. **Aucun panneau ne connaît sa position ou sa taille** — seuls les hôtes de layout (`*View`,
   `*Layout*`, `core/ui/layout/`, fenêtres) appellent `setBounds` ou lisent la géométrie du parent/écran.
3. **`daw_domain` ne lie et n'inclut ni JUCE, ni Tracktion, ni ui.**

Mécanismes :
- `scripts/check_hygiene.py` — analyse statique des trois règles sur `core/`, plus une règle 4 :
  aucun point-virgule dans un nom de `TEST_CASE` (un point-virgule coupe le nom en liste ctest et le
  filtre ne matche plus rien — quatre tests avaient cessé de tourner silencieusement pour cette raison).
- `cmake/DawGuards.cmake` — échoue la configuration si une cible lie `juce::`, `tracktion::`, `daw_ui`
  ou `daw_app`.
- Test croisé registry ↔ table de descriptions du copilote, **dans les deux sens** (S8–S9) : une
  commande ajoutée au `CommandRegistry` sans entrée dans la table du copilote casse le test, et
  inversement.

## 4. Règles de méthode — tenues depuis vingt semaines

- **Un excellent logiciel, même si ça prend plus de temps et de moyens** (3 octobre 2026). Une chose
  mieux faite plus tard, avec plus de moyens, se reporte au lieu d'être faite au rabais pour tenir une
  semaine. **Une semaine est un périmètre, pas une durée** (6 octobre 2026) : elle dure ce qu'il faut pour
  que tout son périmètre soit bien fait ; un chantier ne tombe plus faute de temps, il ne se reporte que s'il
  sera mieux fait plus tard, et le bilan dit où il va.
  Entre deux voies, recommander la meilleure pour le produit et dire franchement ce qu'elle coûte en temps
  et en moyens ; le coût ne la disqualifie pas, le fondateur tranche. La S26 livre un prototype aux
  fondations bien faites, pas un produit poli.
- **On construit, puis on essaie par phases ; aucune semaine n'attend un essai du fondateur** (3 octobre
  2026). Ses essais à la main et à l'oreille viennent après la S26, dans une phase consacrée à
  l'ergonomie. Aucun brief ni bilan ne met son essai en condition bloquante. Chaque bilan garde sa section
  « À essayer, dans l'ordre » et l'ajoute à `docs/essais-apres-s26.md`. Pendant les 26 semaines, n'est
  exigé que ce que la machine prouve : rendu mesuré, `--verify`, tests cassés une fois. Une cause non
  trouvée se dit et se note dans les dettes ; elle n'arrête la semaine que si elle empêche de montrer la
  fonctionnalité.
- **Exposer avant de coder.** Une décision de modèle ou de comportement se présente et se discute avant
  d'être écrite, jamais découverte dans le diff.
- **Un test qui interroge l'état ne prouve pas l'effet.** Trouvé en S3 sur `EngineHostTests` : un test
  qui lit une propriété d'un composant peut être vert alors que le composant ne produit rien. Verrouiller
  la propriété à la bonne couche, pas à la couche qui la lit.
- **Un test neuf qui passe du premier coup est cassé une fois.** On casse volontairement le code qu'il
  regarde pour vérifier qu'il tombe au rouge ; sinon il est aveugle (S17 : un test qui comparait deux
  payloads perdant le nom de la même façon a été trouvé de cette manière).
- **Dire quand un choix contredit l'acquis.** Un écart au modèle établi est nommé explicitement dans le
  bilan de la semaine (voir « Les écarts à l'acquis, dits » dans chaque `docs/bilan-sN.md`), jamais
  glissé en silence.
- **`./scripts/check-all.sh` avant chaque commit.** Lance clang-format, hygiène, validation des
  manifestes workspaces, ruff, pytest — tout ce que la CI vérifie sauf le build et les tests C++
  (les presets, qui prennent des minutes).
- **Commits atomiques.** Un commit, un changement ; un diff qui touche plusieurs sujets à la fois est
  noté comme l'écart qu'il est.
- **`IDEES.md` se remplit et ne se met pas en œuvre.** C'est une décision de roadmap consignée, pas un
  chantier ; rien n'y est construit tant qu'une semaine ne le prend pas explicitement dans son périmètre.

## 5. Où trouver quoi

- **Bilans hebdomadaires** : `docs/bilan-sN.md` (S1 à S25 au 08/10/2026, plus `bilan-s18bis.md`), un par semaine, plus
  `docs/bilan-s7bis.md`. Chaque bilan documente les écarts à l'acquis, les tests cassés une fois, et le
  reste à faire.
- **Plan des semaines S21 à S26** : `docs/plan-s21-s26.md` — base des briefs, pas un brief.
- **Briefs** : `docs/brief-sN.md`, rangés depuis la S24 (les précédents n'ont été donnés qu'en session).
- **Essais du fondateur, après la S26** : `docs/essais-apres-s26.md` — les sections « À essayer » des
  bilans depuis la S17, dans l'ordre ; chaque bilan y ajoute la sienne.
- **Roadmap non engagée** : `IDEES.md` — couche décision du copilote, couche générative, recherche de
  samples par IA, stratégie du moteur génératif.
- **Modèle du Command Bus** : `docs/command-bus.md`.
- **Persistance et journal append-only** : `docs/persistence.md`.
- **Plugins VST3/CLAP** : `docs/plugins.md`.
- **Presets CMake** (`CMakePresets.json`) : `linux-clang` / `linux-clang-release` (CI), `windows-msvc` /
  `windows-msvc-release` (cible produit), `domain-only` (le domaine seul, sans JUCE ni sous-modules).
- **Variantes de `--verify`** (auto-vérification de l'application, rendu audio compris) : `--verify`
  (parcours complet), `--verify-reopen` (fermer/rouvrir dans un autre processus), `--verify-legacy`
  (compatibilité d'un ancien projet), `--verify-file` (menu Fichier, jusqu'à un vrai « Enregistrer
  sous »), `--verify-canvas` (la toile), `--verify-canvas-charge` et `--verify-fluidite` (mesures de
  repeint, Direct2D au 95e centile), `--verify-mix` (le mixage par l'IA sur des signaux connus, par les
  règles, sans clé), `--verify-lecture` (75 lectures de la première mesure après cinq actions, dont la carte
  son perdue ; le chemin audio écrit à chaque silence), `--verify-stems` (une séparation réelle par le modèle
  rapide sur des sources connues mélangées ; toute autre vérification sépare par `--stems-model fake`, les filtres
  de la CI), `--verify-jeu` (jouer et enregistrer par une entrée simulée, latence comprise), `--verify-audio` (la fenêtre
« Audio » sur la carte de la machine : l'essai, le tampon relu au moteur, la latence), `--verify-flux` (les effets de
la tranche, le flux audio, ses gestes, l'écoute seule, la proposition du mixage, le coût des prises, les bus
intelligents), `--verify-kit` (le kit sur une bibliothèque construite, l'index de 10 000 fichiers), `--verify-voix` (le push-to-talk du
micro au projet : des fichiers du jeu d'essai à la place du micro, le transcripteur rejoué, un copilote qui répond par
une table ; `--voix-transcripteur parakeet` prend le vrai modèle, `--voix-micro-reel` ouvre le vrai micro et compare la
sortie). `--micro-ouvert` garde le micro ouvert toute une vérification (pour `--verify-lecture`). `--tampon N` et
`--pilote partage|basse-latence|exclusif` ouvrent la carte ainsi, en vérification seulement. Toute vérification
travaille avec un dossier de réglages de la machine à elle (`<dossier>/reglages-machine`, copie de `Settings.xml` et
`plugins.xml`) : ni la carte, ni l'index des samples de la personne ne sont réécrits. `--sans-jeu` retire le
  plugin du jeu des chaînes, pour comparer `--verify-lecture`. `scripts/verify-quit.ps1 -Repeat N` : la fermeture
  finit le processus, avec et sans copilote, en lecture, sans passer par la garde. `--mix-once` n'est **pas**
  une vérification : un mixage réel par le modèle sur le projet donné, écrit dans `daw.log`, refusé, puis
  quitter (appel payant, hors CI). Toujours avec
  `--project` dans un dossier jetable **et** `--layout <fichier jetable>` : sans `--layout`, elles
  réécrivent `%APPDATA%\DAW IA\DAW IA.layout`. Une vérification ne mixe jamais par le modèle.
- **Workspaces réservés aux ateliers** : `"workshop": true` dans le manifeste (Découverte, depuis le
  03/10/2026). Absents de la barre, refusés par leur nom, une disposition ou une relance qui les nomme
  ouvre le beatmaker ; seul un lancement avec `--atelier` les montre.
- **Clé d'API** : variable d'environnement `DAW_IA_ANTHROPIC_API_KEY`, jamais en dur ni dans un fichier
  du dépôt (vérifié en S17 : aucune clé, aucun `.env`, dans tout l'historique).
- **Tests d'un plugin réel** : label ctest `audio`, exclus de la CI ; `DAW_TEST_VST3` / `DAW_TEST_CLAP`
  en variables d'environnement.

## 6. Dettes ouvertes

| Dette | Depuis | État |
|---|---|---|
| Bug intermittent de la lecture | S12 | **fermé S21** : la carte son (casque Bluetooth) perdue sans réouverture ; `AudioOutputKeeper` rouvre la sortie de Windows et revient à la première |
| Écoute de la zone multi-pistes (accords/basse/mélodie ensemble, à l'oreille) | S17 | **phase d'essais après la S26.** Pas encore faite, décrite comme le test le plus important de la semaine |
| « Ranger le projet » comme geste explicite | S17 (§6) | demandé en cours de semaine, pas engagé |
| Fine-tune sur un corpus personnel du fondateur | tranché S15 | écarté définitivement — un générateur enfermé dans le style d'un seul producteur ne sert que lui |
| Stratégie complète du moteur génératif au-delà de S15 | ouvert depuis S14 | partiellement tranchée (S15) ; le reste (empiler les couches, Markov vs neuronal) reste à trancher |
| L'application qui ne quitte pas à la fermeture (fenêtre partie, processus vivant) | S18 | non reproduite en 44 fermetures (S19, S21 avec copilote et en lecture) ; cause non trouvée, garde S19 en place |
| Le piano-roll encore là, alors que la toile fait tout ce qu'il faisait | S19 | **phase d'essais après la S26** ; personne n'y touche d'ici là. Le retirer réécrit des étapes de `--verify` et `--verify-fluidite` |
| La toile chargée au-dessus de 8 ms au 95e centile en Direct2D (256 blocs) | S19 | 9 à 13 ms selon la charge de la machine, mesuré avec Chrome actif |
| Le glissé d'un point d'automation salit plus que sa bande (12 à 14 ms de repeint par image) | S19 | cause non trouvée |
| La mesure du mixage de 16 pistes de 3 minutes, à peine sous 20 s | S20 | 15 à 19 s machine au repos en fin de semaine, 27 à 28 s en cours de semaine machine chargée ; cause de l'écart non démontrée ; deux pistes d'accélération essayées et retirées |
| L'avant/après sur trois morceaux du fondateur | S20 | **phase d'essais après la S26** (critères dans `docs/essais-apres-s26.md`) ; plus une dette de développement ; `--verify-mix` ne couvre que des signaux connus |
| Les plugins de la personne ne traitent que la piste d'instrument, pas ses enregistrements (piste compagne) | S20 | construit S21 sur la branche `s21-piste-compagne` (une tranche par piste, prouvée au rendu) ; **non fusionné** : la lecture en direct y devient muette une fois sur trois (`--verify-lecture`), cause non trouvée |
| La compilation incrémentale laisse un objet périmé (binaire qui plante ou panneaux « à venir ») | S20, revu S21 | **fermé S22** : le préfixe `/showIncludes` de MSVC en français, illisible par Ninja hors page de code 65001 ; compiler par `scripts/build.cmd` |
| Les tests du moteur partagent les réglages persistés de l'engine de test (l'identité CLAP retient `C:\dawS9\…`) | S21 | **fermé S22** : un dossier de réglages jetable par processus |
| Les poids HTDemucs sont donnés « for research purpose », pas sous MIT | S22 | choix du fondateur de bâtir le prototype dessus ; téléchargés au premier usage, jamais livrés ; une licence à chercher avant l'installeur de la S26, SCNet XL IHF à trancher |
| L'arrangement ne lit la direction qu'à travers le copilote (sections de la référence, en secondes) | S22 | aucun code n'arrange d'après les sections ; un modèle à exposer avant d'écrire |
| `--verify` complet pas relancé après les stems et la direction | S22 | **fermé S24** : 674 sur 674 avec copilote au chantier 0, et en fin de semaine au second passage (le premier : 667, des fenêtres non vues affichées pendant une minute, cause non trouvée) |
| `--verify-lecture` avec le jeu : à 0 sur 75 ? | S23 | **fermé S24** : 0 sur 75 avec et sans `--sans-jeu`, et sur le code final de la S24, prises du flux comprises |
| La fenêtre « Audio » (pilote, tampon, latence) | S23 | **fermé S24** (F12, conseil mesuré) |
| Le mode exclusif décroche sur la Realtek du fondateur | S24 | une quarantaine de décrochages en 3 s à chaque tampon, au pire 75 ms ; le conseil reste le partagé ; `--verify-lecture` en exclusif 256 : 1 sur 75 |
| Le repli de la carte sur « Windows Audio » partagé | S24 | **fermé S25** : `--verify-audio` perd une carte en exclusif (simulé) et se replie, puis revient |
| `scripts/verify-quit.ps1` utilise les réglages de la personne | S24 | **fermé S25** (`--reglages`) |
| Les prises du flux décalaient le relevé de `--verify-lecture` d'environ 50 ms | S24 | le relevé se lit maintenant par position (0 sur 75) ; la cause du décalage n'est pas trouvée |
| « F3 rouvre la fenêtre » dans `--verify-flux` | S24 | **expliqué S25** : la fenêtre principale réduite pendant que Chrome était devant ; la vérification le dit (« passage non probant à l'écran ») |
| `MeterTests`, « fader -6 dB », intermittent | S24 | 1 fois sur 4 ; probablement la phase de 4OSC, non prouvé |
| La contrainte de couleur du kit sur une petite bibliothèque | S24 | **fermé S25** : sous dix samples, l'élément le plus proche « hors couleur », son écart dit |
| La réverbération par envoi (bus intelligents) non éprouvée au rendu | S24 | **fermé S25** : test `audio` avec un VST3 donné ; la queue au même niveau, le son sec ajouté par l'envoi mesuré (+7,2 dB) |
| 4OSC garde une voix au-delà de 32 (voix volée, relâché pendant la coupure) | S23 | défaut de Tracktion prouvé au rendu ; non contourné |
| Une note tenue coupée quand la tête est déplacée pendant la lecture | S23 | indiscernable d'un tour de boucle pour le plugin du jeu |
| Les décalages de couleur des règles du mixage ignorent `direction.amount` | S23 | **fermé S24** (`4fb3fbd`) |
| La ligne de génération lue un geste en retard, par intermittence | S19 | l'étape attend maintenant la fin de la lecture ; la cause de la variante n'est pas trouvée |
| La catégorie « Fx » de Valhalla : la page « Bus » ne propose pas ses réverbérations | S25 | **tranché le 8 octobre 2026** : reconnaître par le nom, avec des suggestions à accepter dans les cas durs ; exposé avant d'écrire |
| Le job Checks de la CI bloqué sur `apt-get update` | S25 | **fermé S25** (`eaf2a71`) : clang-format 18.1.8 depuis PyPI |
| `--verify-voix` pas lancé par la CI (aucune vérification ne l'est) | S25 | **fermé S25** (`2535573`) : sur le job Windows, 198 sur 198, sans modèle, ni clé, ni micro ; 9 min sur le runner |
| Magenta RealTime 2 à intégrer | S25 | **décidé le 8 octobre 2026, pendant l'incubation** ; temps réel sur Apple Silicon seulement selon sa carte, la machine du fondateur n'a pas de GPU |
| Le taux d'erreur de la voix sur ta voix | S25 | **phase d'essais après la S26** : le jeu d'essai est en voix de synthèse (10,2 %) |
| Un micro absent ou refusé par Windows, le micro d'un casque Bluetooth ouvert | S25 | jamais éprouvés par une vérification |
| L'écran « À propos » doit citer NVIDIA (poids CC-BY-4.0 de Parakeet) | S25 | S26 |
| `--verify` complet à 672 sur 675 (étapes 58 et 60, vu-mètres muets au premier tour de boucle) | S25 | deux fois de suite en Release ; bissection commencée à `a782ac5`, pas finie ; cause non trouvée |
| Support Ubuntu en cible produit | reporté S1/S5 | scripts gardés pour la CI seulement |
