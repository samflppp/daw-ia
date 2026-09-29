# Bilan de fin de S17 — DAW IA

**Période :** semaine 17 sur 26. Rédigé le 28 septembre 2026.
**Dépôt :** `samflppp/daw-ia`, branche `main`, push direct. La fusion S16 (`6c8eb6a`), puis 7 commits de
`d0f7031` à `fb29ec5`, plus ce bilan.
**Volume depuis la fusion S16 :** 52 fichiers, +4 626 lignes, −216 (hors ce bilan).
**CI :** verte sur `fb29ec5`, Linux et Windows.
**Tests :** 367 cas ctest (domaine, persistance, interface ; +18), 89 cas d'engine (+2), 42 cas Python.
**Registry :** 59 types (+4 : `lane.create`, `lane.remove`, `lane.rename`, `lane.move`).
**Schéma du projet :** 5 (+1, sans colonne). **Enveloppe :** v3, inchangée.

## En bref

- **Chantier 0.** Aucun secret dans l'historique (183 commits, toutes branches). La S16 n'était fusionnée qu'en
  partie : quatre commits, dont la correction MSVC et `bilan-s16.md`, manquaient à `main`. Fusionnés en `6c8eb6a`,
  CI verte.
- **La playlist façon FL.** Des lignes libres, dans le projet ; un bloc passe d'une ligne à l'autre en le glissant.
  Les projets des seize semaines se rouvrent à l'octet, et sonnent pareil.
- **La zone multi-pistes.** Alt + glisser sur plusieurs lignes, un prompt : accords, basse et mélodie, chacun sur
  sa ligne et sa piste, harmonisés entre eux, entendus avant d'être écrits, écrits en une entrée d'historique.
- **Demandé en cours de semaine :** un classifieur de rôle local, et le nom du canal suggéré d'après le preset.
- **Tombé :** « Ranger le projet » comme geste dédié (§6).

## 1. Chantier 0

| Point | Constat | Action |
|---|---|---|
| Secrets dans l'historique | Rien. Clés Anthropic, OpenAI, GitHub, AWS, Slack, Google, Hugging Face, clés privées, affectations `api_key = "…"`, fichiers `.env`/`.pem`/`id_rsa`/`.npmrc` : aucun. La seule clé du projet, `DAW_IA_ANTHROPIC_API_KEY`, n'apparaît que comme nom de variable. | Aucune. Les forks et caches GitHub sont hors de ma portée. |
| S16 dans `main` | PR #1 fusionnée, mais `fa2fa82`, `2c3d3c1`, `dbba3a8` (MSVC) et `cae68a1` (bilan) poussés après. Fusion sans conflit, arbre identique à la pointe S16, qui était verte. | Fusion `6c8eb6a`, sur ton accord. CI verte. |

## 2. Le modèle de ligne retenu

Exposé et validé avant la première ligne. Tes réponses : « tout en ligne libre », puis « Go ».

| Question | Réponse | Pourquoi |
|---|---|---|
| Ce qu'est une ligne | `Lane { LaneId id; std::string name; }`, ordonnée, dans `ProjectState` à côté de l'arrangement | L'utilisateur l'écrit : la perdre à la réouverture ou sur Ctrl+Z serait un bug |
| Ce qui change le raisonnement de la S10 | La S10 écartait une ligne *déduite* (« ligne n = n-ième pattern »), qui ne porte aucune information. Une ligne libre en porte : « ce bloc est sur Basse ». Le critère devient : si la perdre est un bug, c'est le domaine | — |
| Ce qui reste à l'écran | Hauteur, couleur, défilement | Ce ne sont pas des choses que l'on défait |
| Ce qu'une ligne ne fait pas | Sonner : ni volume, ni mute | C'est ce qui garde la projection et l'écoute intactes |
| Le champ du bloc | `laneId` sur `Placement` et `AudioClip` : un identifiant, pas un rang | Réordonner les lignes ne réécrit aucun bloc |
| Sans lui | La ligne de son pattern (ou de sa piste pour l'audio), dont l'identifiant est celui du pattern (ou de la piste), retypé | Dérivé, pas engendré : même principe que `patternIdForClip` en S9 ; un rejeu donne toujours les mêmes lignes |
| Les automations | Restent des courbes sous les lignes | Ce ne sont pas des blocs : proposé, accepté implicitement par « Go » |

**Les commandes.** Toutes annulables, identifiants fournis par l'appelant.

| Commande | Ce que garde l'annulation |
|---|---|
| `lane.create` (id, nom, rang) | — |
| `lane.remove` | la ligne, son rang, chaque bloc et son rang (comme `pattern.remove`) |
| `lane.rename` | l'ancien nom |
| `lane.move` | l'ancien rang ; fusionnée par geste |
| `placement.move`, `audio.move` + `laneId` facultatif | l'ancien temps et l'ancienne ligne ; un glissé = une entrée |
| `pattern.place`, `audio.place` + `laneId` facultatif | la ligne créée, s'il a fallu la créer |
| `pattern.create` + `ownLane` (vrai par défaut) | la ligne créée |
| `pattern.remove` | la ligne de son pattern, retirée si elle reste vide et sans nom |

**Projection et écoute.** `ProjectProjector` ne lit pas `laneId`. Prouvé au rendu (§4).

## 3. Ce que la migration a coûté

**Zéro octet réécrit.** Le journal garde ses payloads ; un payload sans ligne vaut la ligne S16. `toValue` laisse
les lignes dehors tant que ce sont celles de la S16 (une par pattern, puis une par piste qui tient de l'audio), et
`laneId` quand c'est celle du bloc : un projet ancien se sérialise, et donc se hache, comme avant.

**Schéma 5, sans colonne,** comme le 4. Il ne sert qu'à l'autre sens : un build S16 rejouerait `placement.move` en
lisant le temps et pas la ligne, sans rien dire. Il refuse désormais le projet.

**Ce que ça a coûté en code :** une règle d'insertion pour recréer l'ordre S16 à la volée (lignes de pattern
au-dessus des lignes audio, pistes dans l'ordre), et la ligne du pattern retirée par `pattern.remove` quand elle
reste vide : sans elle, un projet S16 où un pattern a été supprimé se rouvrait avec une ligne vide de trop.

**Ce qui n'est pas repris :** retirer le dernier clip audio d'une piste, ou la piste, laisse sa ligne (vide). En S16
elle disparaissait. Le son et l'état sont justes ; l'écran a une ligne vide de plus, qu'un clic droit supprime.

**La preuve** (`persistence`, processus enfants, comme en S5 et S10) :
1. un processus écrit un projet S16 : deux pistes, un pattern posé deux fois, un autre une fois, un sample ; les
   payloads sont ceux de la S16 ; il repose le schéma à 4 et meurt ;
2. un deuxième processus l'ouvre : **même état, à l'octet**, et le mot « lane » n'y figure pas ;
3. le parent l'ouvre (migration 4 → 5), trouve trois lignes dans l'ordre S16, glisse la basse sur la ligne des
   drums, nomme une ligne, en crée une, y range le sample, ferme ;
4. un troisième processus rouvre : les lignes sont là ; quatre Ctrl+Z rendent le projet S16, à l'octet.

Cassé une fois : `placement.move` qui n'écrit plus sa ligne dans le journal. Trois vérifications passent au rouge.

## 4. Chantier 1 — l'écran

| Geste | Effet |
|---|---|
| Glisser un bloc verticalement | Il change de ligne ; sous la dernière, « + Nouvelle ligne » en crée une, dans la même entrée d'historique |
| Sélection, Ctrl+C/V, Ctrl+B | Chaque bloc garde sa ligne ; une ligne supprimée depuis rend le bloc à celle de son pattern |
| Aperçus de notes, formes d'onde | Suivent le bloc, pendant le glissé aussi |
| Nom d'une ligne | Glisser : réordonne. Double-clic : renomme. Clic droit : renommer, insérer, supprimer avec ses blocs, et les verbes du pattern pour qui la ligne a été faite |
| Clic sur une ligne | Pose le pattern pour qui elle a été faite (l'acquis S10), sinon le pattern en cours, comme FL |
| Déposer un sample | Sur la ligne visée ; sous les lignes, sur celle de sa piste |

Une ligne sans nom dit ce pour quoi elle a été faite (son pattern, sa piste), sinon « Ligne 4 ».

**La preuve au rendu** (`ArrangementTests`) : quatre poses, deux changées de ligne, la ligne déplacée et renommée.
0 clip inséré, 0 réécrit, 0 déplacé dans l'Edit ; mêmes attaques ; même niveau temps par temps, à 2 % près.
**Pas au bit près :** deux rendus d'un Edit inchangé diffèrent déjà (4OSC tourne libre). Je l'ai mesuré avant
d'écrire la tolérance. Cassé une fois : une projection qui saute les blocs rangés ailleurs que sur la ligne de
leur pattern ; le test passe au rouge.

## 5. Chantier 2 — la zone multi-pistes

**Le geste.** Alt + glisser sur plusieurs lignes : un rectangle calé sur la mesure, et « ✦ Générer » au coin.
Ctrl+G ou la pastille ouvre la fenêtre de la S16 sous la playlist. Entrée lit le prompt (le copilote, sinon le
lecteur local) ; les parties apparaissent en gris, avec leurs notes, là où elles seraient écrites ; Ctrl+Espace
les fait entendre ensemble ; Tab les écrit ; Échap ferme.

**Les rôles** viennent de `tidy::classifyLane` : le nom de la ligne d'abord (« Basse », « Accords », « Lead »),
sinon ce qui y est rangé. La piste est celle de la ligne qui joue ce rôle, sinon une piste du projet qui le joue.
Une ligne illisible, ou sans piste pour son rôle, est laissée de côté et la fenêtre le dit.

**L'harmonie sans toucher au moteur.** Le générateur lit son harmonie dans les autres lignes *du même pattern*.
La zone tire donc les parties dans l'ordre accords → basse → mélodie → rythme, chacune sur une copie du projet où
les précédentes sont écrites dans son pattern. Le générateur fait ce qu'il a toujours fait ; la cohérence vient de
ce qu'on lui donne à entendre.

| Mesure (`ZoneProposalTests`) | Résultat |
|---|---|
| Trois lignes Accords, Basse, Mélodie ; pistes Keys, 808, Pluck | trois parties, chacune sur sa ligne et sa piste |
| « boucle trap F#m » | toutes les notes en fa dièse mineur |
| Première note de basse de chaque mesure sur une note de l'accord | 4 mesures sur 4 ; **3 sur 4 quand la cascade est coupée** |
| Avant Tab | projet identique à l'octet, historique inchangé |
| Tab | une entrée d'historique, trois patterns, un bloc sur chaque ligne ; un Ctrl+Z rend le projet à l'octet |
| Un bloc sous la zone | la partie s'écrit dedans, pas à côté |
| Une variante de plus | toutes les parties changent ; la basse suit les nouveaux accords |

**L'écoute** joue la proposition entière. C'est le seul endroit où `core/engine` change, et je l'avais annoncé :
`Audition` prend un pattern à créer (`newAtBeats`), posé le temps de l'écoute. Au rendu : deux patterns inexistants
sur deux pistes s'entendent à leur temps, partent avec l'écoute, le projet reste à l'octet. Cassé une fois.

**Le moteur ne change pas :** Harmony, Form, Transform, le contrat des contraintes sont intacts.

## 6. Demandé en cours de semaine : les noms, et le rangement

**Le classifieur de rôle** (`domain/tidy/Roles.h`) : local, déterministe, rien de stocké. Le nom choisi, puis le
preset, le sample, le plugin, puis les notes (empilées : accords ; tenues : nappe ; graves : basse). Il dit
d'où vient chaque lecture : « d'après le preset « Sub Bass » ». Premier passage : « Bass Drum » était lu comme une
basse. L'ordre des mots-clés décide maintenant, les mots de batterie d'abord.

**Le nom du canal d'après le preset.** `PluginHost::presetName` lit le programme qu'annonce l'instrument. Le
channel rack affiche « → Basse » en gris à côté d'un nom que personne n'a choisi (« Piste 3 », le nom du plugin).
Un clic l'accepte ; rien n'est renommé sans ce clic.
**Limite honnête :** beaucoup de synthés ne montrent leur preset que dans leur propre fenêtre, et un CLAP répond par
son nom. Alors ce sont le sample et les notes qui parlent. **Non vérifié sur un vrai VST3** : pas de plugin dans
l'environnement de construction. La logique de lecture, elle, est testée (`RoleTests`).

**Tombé : « Ranger le projet »** comme geste dédié (proposer en une fois noms, lignes nommées, blocs rangés).
Le copilote en a les moyens dès maintenant : il voit les lignes nommées et a les outils `lane.*` et
`placement.move` avec `laneId`. Noté dans `IDEES.md`.

## 7. La règle du test cassé une fois

Chaque test neuf passé du premier coup a été cassé une fois dans le code qu'il regarde ; tous sont passés au rouge,
sauf un.
- **Un test aveugle trouvé :** le tour du registre comparait deux payloads qui perdaient le nom *de la même façon*.
  Il applique maintenant la commande reconstruite et lit le nom dans l'état.
- 8 cassures sur les lignes, 5 sur le classifieur, 5 sur la zone, 2 sur le projecteur, 1 sur la migration.

## 8. Vérifications de l'application

Les vérifications de l'application ont tourné sous Linux (Xvfb), sans carte son et sans copilote. Pour savoir ce
qui vient de la semaine, le même passage a tourné sur `main` d'avant la semaine (`6c8eb6a`), chacun dans un dossier
de configuration vierge.

| Passage | Passées | En échec |
|---|---|---|
| `main` avant la S17 | 470 | 61 |
| `main` après la S17 (`fb29ec5`) | 494 | 61 |

**Les 61 échecs sont les mêmes, un pour un :** pas de carte son (lecture, vu-mètres, écoute), pas de copilote,
export MP3/AAC réservé à Windows, et ce qui en découle. Aucun ne vient de la semaine.

| Étape S17 | Résultat ici |
|---|---|
| Glisser un bloc vers le bas : une ligne naît, le bloc garde son temps, une entrée d'historique, mêmes attaques au rendu, Ctrl+Z à l'octet | OK |
| Trois lignes Accords, Basse, Mélodie ; Alt + glisser : une zone de quatre mesures, rien d'écrit | OK |
| Ctrl+G, « boucle trap F#m » : accords sur Keys, basse sur 808, mélodie sur Pluck, chacun sur sa ligne, tout en fa dièse mineur ; « quatre mesures en Fa# mineur : accords, basse et mélodie » ; rien d'écrit | OK |
| ▶ Écouter : 8 attaques dans la zone au rendu, projet et historique intacts | OK |
| Tab : une entrée, trois patterns, un bloc au début de la zone sur chaque ligne ; Ctrl+Z à l'octet | OK |

**Un rouge qui ne regardait pas le bon objet.** Au premier passage, « un bloc sur chaque ligne » était rouge : viser
le second coin de la zone faisait défiler la vue et décalait le premier, et la zone ne faisait que deux mesures.
C'était le geste simulé, pas le produit. Corrigé dans la vérification (`fb29ec5`), comme les autres étapes le font
déjà : les deux coins à l'écran avant de viser.

**Un passage faussé, écarté.** Deux passages lancés l'un après l'autre partagent le dossier de configuration : le
second rouvre le projet du premier (272 commandes rejouées) et 40 étapes tombent. Chaque passage a maintenant son
propre dossier.

**Moteur :** 87 cas sur 89 ; les deux rouges demandent une carte son (`EngineHostTests`), comme avant la semaine.
**CI :** verte sur `fb29ec5`, Linux et Windows. Les commits intermédiaires ont été compilés et testés au preset
domaine ; l'application entière l'est sur le dernier.

## 9. Les écarts à l'acquis, dits

- **Le format change :** schéma 5, `laneId`, `lanes`. Exposé, validé, migré sans réécriture.
- **`core/engine` change** pour l'écoute d'un pattern à créer. Annoncé dans le modèle.
- **Le clic sur une ligne** qui n'est pas celle d'un pattern pose le pattern en cours (FL), plus « le pattern de la
  ligne » : cette notion n'existe plus pour une ligne libre. Sur la ligne d'un pattern, l'acquis S10 tient.
- **Le lecteur local ne lit pas « fa# mineur »** en toutes lettres, seulement « F#m ». C'est le moteur de lecture,
  pas touché ; le copilote, lui, traduit. Noté dans `IDEES.md`.

## 10. À essayer et à écouter, dans l'ordre

Sur `main`, compilée chez toi, avec carte son et clé d'API.

1. **Un projet d'avant.** Ouvre un projet de la S16. Les lignes doivent être celles que tu avais, dans le même
   ordre. Lance la lecture : rien ne doit avoir changé à l'oreille.
2. **Glisser vers le bas.** Prends un bloc, descends-le sur « + Nouvelle ligne » : une ligne naît. Ctrl+Z :
   tout revient. Recommence avec trois blocs sélectionnés.
3. **Ranger à la main.** Double-clique le nom d'une ligne, appelle-la « Drums » ; glisse son nom tout en haut.
   Ctrl+C / Ctrl+V un bloc : la copie doit rester sur sa ligne.
4. **La zone.** Trois canaux (Keys, 808, Pluck), trois lignes nommées Accords, Basse, Mélodie. Alt + glisser
   sur les trois, quatre mesures ; « ✦ Générer » ; « une boucle trap en fa# mineur », Entrée.
5. **L'écoute de la zone.** Ctrl+Espace : les trois parties ensemble. **C'est le test le plus important de la
   semaine :** la basse suit-elle les accords à l'oreille ? Change de variante (flèches) : tout doit bouger
   ensemble.
6. **Tab,** puis Ctrl+Z : trois blocs apparaissent, puis disparaissent d'un coup.
7. **Le nom par le preset.** Un canal sur un VST3 dont le preset s'appelle « … Bass … », nom par défaut :
   « → Basse » doit apparaître en gris dans le channel rack. Dis-moi quels synthés annoncent leur preset.

## 11. Après le bilan : un seul accord au lieu d'une progression

Ton premier essai sur ta machine : « fais des accords triste sur le omnisphere » donnait **un seul accord** tenu.
Le générateur change d'accord à chaque mesure, et le copilote avait généré sur un pattern d'**une** mesure, la
longueur par défaut. Le moteur n'était pas en cause. Le copilote allonge maintenant le pattern à quatre mesures
(`pattern.set_length`, ou `clip.create_midi` de 16 temps) avant de générer, sauf longueur demandée (`e707259`, un
test Python, cassé une fois). Ton écoute : quatre mesures en la mineur, « ça sonne bien triste ».

## 12. Reste à faire

- La zone multi-pistes à l'oreille (§10, étapes 4 à 6) : le test le plus important, pas encore fait.
- Ton écoute (§10), et les vérifications de l'application sur ta machine, avec carte son et copilote.
- « Ranger le projet » (§6), si tu le veux toujours comme geste.
- Le bug intermittent : il ne s'est pas présenté cette semaine.
