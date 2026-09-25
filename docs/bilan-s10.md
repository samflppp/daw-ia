# Bilan de fin de S10 — DAW IA

**Période :** semaine 10 sur 26. Rédigé le 23 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 10 commits S10 (`295d36d` → `HEAD`).
**Volume :** 37 fichiers, +2 657 lignes, −230 (IDEES.md compris).
**Tests :** 233 cas hors audio (domaine et persistance), 61 cas d'engine sous label `audio`,
18 cas Python, au dernier passage complet.
**Registry :** 42 types de commandes, contre 33 la semaine dernière (38 pour la playlist, 4 pour les
samples).
**Schéma de projet :** 4. Les samples ajoutent deux champs optionnels, écrits seulement s'ils
servent : un projet sans sample s'écrit à l'octet près comme avant.

**Mis à jour le 24 septembre** avec la seconde demande de la semaine : barre espace, navigateur de
samples, canal sampler, clips audio, sélection et copier-coller façon FL (§8 ter).

La S9 avait séparé le contenu de sa position. Cette semaine, la position est devenue un écran : la
playlist. Et l'affirmation « modifié une fois, change partout » est maintenant prouvée par un rendu
audio, plus seulement par une lecture de l'état.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 0 | Modèle de placement et bascule pattern/chanson exposés et validés avant de coder | Livré | les quatre points validés : une ligne par pattern, pas de redimensionnement, `transport.set_mode`, quatre verbes |
| 1 | Panneau `playlist` déclaré dans `beatmaker.json` | Livré | §4 ; huit panneaux |
| 2 | Poser, déplacer, supprimer, par le bus, annulables, identifiants fournis par l'appelant | Livré | §2 ; pas de redimensionnement, par décision |
| 3 | **La preuve qui compte** : posé huit fois, modifié une fois, les huit changent | Livré, au rendu et dans l'application | §3.2 ; §8, point 5 |
| 4 | Le transport suit la playlist, les modes pattern et chanson coexistent | Livré | §3.3 |
| 5 | Projection par identité, sans reconstruire ce qui n'a pas changé, piège du tempo compris | Livré | §3.1, compteurs et identité d'objet |
| 6 | Commandes dans la table du copilote ; « répète le pattern 1 huit fois puis ajoute le pattern 2 » | Livré : une entrée copilote, un Ctrl+Z | §5 ; §8, point 7 |
| 7 | Les projets des semaines précédentes se relisent | Livré | §6 ; trois processus, 22 ms puis 28 ms |
| 8 | Demandé en cours de semaine : pages en fenêtres | Livré | §8 bis |
| 9 | Demandé en cours de semaine : espace, samples, glisser-déposer, sélection, Ctrl+C/V/B | Livré | §8 ter |

## 2. Le modèle de placement retenu

Il n'a rien changé au modèle de la S9, et c'était l'objectif.

| Question | Réponse | Pourquoi |
|---|---|---|
| Une ligne de playlist par quoi ? | **par pattern**, déduite de l'ordre des patterns | un `Placement` ne porte pas de piste. Avec une ligne par piste, glisser un bloc déplacerait aussi le pattern sur toutes les autres pistes |
| Où sont stockées les lignes ? | nulle part | la ligne n = le n-ième pattern. Aucun état d'écran dans le domaine, aucun champ `lane` |
| Redimensionner une pose ? | non | la longueur appartient au pattern. Étirer une pose sur huit ne doit pas étirer les sept autres. Une pose tronquée serait un `lengthBeats` optionnel sur le `Placement` : un ajout sans migration, reporté |

Les quatre commandes neuves :

| Commande | undoRecord | Coalescence |
|---|---|---|
| `placement.move` | l'ancien temps | par placement, sur le geste : un glissé = une entrée |
| `placement.remove` | le placement **et son rang** dans l'arrangement | — |
| `pattern.rename` | l'ancien nom | — |
| `pattern.remove` | le pattern, son rang, ses lignes, **chaque placement avec son rang** | — |

Les rangs sont conservés pour qu'une annulation rende *le même* arrangement, pas seulement un
arrangement équivalent. Deux ordres différents donnent deux formes sérialisées différentes. Un
test vérifie l'égalité à l'octet près après l'annulation. `ProjectState` a gagné `placementIndex`
et `insertPlacement`.

## 3. Moteur et transport

### 3.1 Réconciliation par identité

Avant, une note ajoutée effaçait puis reposait **tous** les clips de la piste. Maintenant, chaque
clip Tracktion porte une clé (`placement:ligne`, ou `audition:ligne` en mode pattern). Le
projecteur ne touche un clip que sur ce qui a changé :

| Changement | Effet dans l'Edit | Mesuré par |
|---|---|---|
| déplacer une pose parmi huit | 1 clip repositionné, 0 inséré, 0 réécrit, **les huit mêmes objets** (`EditItemID`) | compteurs `stats()` et identités |
| une note ajoutée à un pattern posé 8 fois | 8 séquences réécrites, 0 insertion, 0 déplacement | compteurs |
| **tempo 120 → 60** (le piège de la S7) | 4 clips replacés en secondes, 0 séquence réécrite ; le beat 12 passe de 6 s à 12 s | compteurs et positions |
| déplacer un fader | aucun clip touché | l'instantané de piste est séparé de l'instantané de ce qu'elle joue |

La boucle a eu le même traitement. Elle est recalculée à chaque projection et replacée quand le
tempo bouge. Avant, un changement de tempo laissait la boucle sur les anciennes secondes : c'était
le même piège, à un autre endroit.

### 3.2 La preuve au rendu

`ArrangementTests.cpp` fait un rendu hors ligne de l'Edit, puis écoute le premier quart de chaque
temps :

| Situation | Durée du rendu | Temps qui sonnent |
|---|---|---|
| un pattern de 4 temps, un coup sur le temps 1, posé 8 fois | **16 s** | **8** (temps 0, 4 … 28) |
| une seule `note.add` au temps 3 **du pattern** | 16 s | **16** (les 8 d'avant + 2, 6 … 30) |
| un Ctrl+Z | 16 s | 8 |
| mode pattern sur un autre pattern | **2 s** | 1, seul, en boucle 0 → 2 s |

### 3.3 Mode pattern et mode chanson

Comme validé :

- `transport.set_mode {mode, patternId}` est une commande **transitoire** : ni historique, ni
  journal. Le mode vit dans `TransportState`, comme la tête de lecture. Le pattern est dans le
  payload, jamais lu sur un écran, donc le copilote peut basculer lui aussi.
- **Mode pattern :** l'Edit ne contient que le pattern auditionné, au temps 0, et boucle sur sa
  longueur. Si le pattern s'allonge, la boucle s'allonge sans aucune commande de transport.
- **Mode chanson :** tous les placements, et la boucle de `transport.set_loop` s'il y en a une.
- Changer de mode ramène la tête de lecture au début : le temps 40 de la chanson n'existe pas
  dans un pattern de 4 temps.
- Le transport a deux boutons, **PAT** et **SONG**. L'application s'ouvre en mode pattern sur le
  premier pattern.
- Le rack et le piano-roll suivent le pattern courant (`patternEditing::follow`). Le rack
  resynchronise l'audition **en différé**, hors de la notification du bus, quand une annulation ou
  un `pattern.remove` retire le pattern écouté.

**Un changement de comportement de la S9, dit plutôt qu'appliqué en douce :** « + Pattern » ne
pose plus le pattern sur la timeline. En S9 c'était nécessaire, parce que la boucle avait besoin
d'une pose pour faire entendre le pattern. Le mode pattern joue le pattern là où il est, donc
chaque nouveau brouillon n'encombre plus la chanson. C'est ce que fait FL. Aucune architecture
n'est touchée.

## 4. La playlist à l'écran

C'est une page (F5) du beatmaker en fenêtres (§8 bis).

| Geste | Commande |
|---|---|
| clic dans la ligne d'un pattern | `pattern.place` à la mesure sous le pointeur (Maj : au temps) |
| glisser un bloc | `placement.move`, un geste, une entrée d'historique |
| clic droit sur un bloc | `placement.remove` |
| clic sur l'en-tête de ligne | sélectionne le pattern ; le rack, le piano-roll et le mode pattern suivent |
| clic droit ou double-clic sur l'en-tête | renommer ; supprimer le pattern |
| clic dans la règle | déplace la tête, en mode chanson |

Les poses du pattern courant sont en couleur pleine et les autres en couleur atténuée. Quand on
modifie le rack, on voit donc d'avance tous les endroits qui vont changer. La timeline montre au
moins 16 mesures, plus 4 mesures libres après la dernière pose.

## 5. Le copilote

- Les 5 nouvelles commandes sont dans la table. Le test croisé registry ↔ table passe dans les
  deux sens (38 = 38 pour la playlist, 42 = 42 avec les samples).
- L'état résumé donne à chaque pattern son **rang** (`rank`) et le **libellé** affiché (`label`).
  Ainsi « le pattern 2 » désigne la même chose pour l'utilisateur et pour le modèle. Il donne
  aussi `arrangementEndBeats`, la fin du morceau, et le mode de lecture.
- Le prompt apprend qu'on répète un pattern avec N `pattern.place` bout à bout, **jamais** en
  créant un nouveau pattern.

**Essai en vrai, hors CI** (vrai Claude Sonnet, faux DAW servant l'état et les schémas de cette
semaine). « répète le pattern 1 huit fois puis ajoute le pattern 2 » donne **un seul groupe** de
8 `pattern.place` : le pattern 1 aux temps 16, 32 … 112, puis le pattern 2 à 128. La pose
existante au temps 0 compte comme la première. 4 967 tokens en entrée, 1 412 en sortie.

**Ce que cet essai ne prouve pas :** l'effet à l'écran, et le fait qu'un seul Ctrl+Z défait tout.
Côté domaine, un groupe = une entrée d'historique : c'est acquis depuis la S8, et le test de
migration (§6) le vérifie sur un groupe `origin=copilot` relu depuis le disque. Le point 7 du §8
reste à faire par toi.

## 6. Migration

**Il n'y a pas de schéma 5.** La playlist n'ajoute aucun champ, seulement des types de commande.
Un build plus ancien les refuse de lui-même : son registry ne les connaît pas.

Le test fait tourner trois processus :

1. un enfant écrit un projet S8 (`clip.create_midi`), sur le schéma 3 ;
2. un autre enfant l'ouvre, le migre vers le schéma 4, et y ajoute ce que le rack de la S9
   écrivait (pattern + pose en un groupe, une ligne ouverte par la première case, un trait) ;
3. le parent (S10) rouvre le projet : **22 ms pour 13 lignes**. Il applique les verbes de la
   playlist (7 poses dans un groupe copilote, un déplacement, un renommage, un retrait), puis
   ferme ;
4. un enfant rouvre le projet et rend son état. Le parent rouvre à son tour : **28 ms pour
   23 lignes**. Les deux états sont identiques, la profondeur d'historique aussi, et **quatre
   Ctrl+Z rendent le projet de la S9 à l'octet près**.

**Limite, comme en S9 :** aucun binaire S8 ou S9 n'est réellement lancé. Les enfants écrivent
avec les commandes d'aujourd'hui, dont les payloads n'ont pas changé ; le test épinglé en S9 sur
`clip.create_midi` le garantit.

## 7. Ce que je n'ai pas fait, et pourquoi

| Point | Pourquoi |
|---|---|
| Redimensionner une pose | décision validée ; §2 |
| Défilement et zoom de la playlist | la timeline s'adapte à la largeur. Au-delà d'une soixantaine de mesures, les blocs deviennent étroits |
| Écoute d'un sample dans le navigateur | un clic sur un sample ne le joue pas encore ; il faut le déposer pour l'entendre |
| Réglages du sampler (note de base, enveloppe, hauteur) | le canal joue le sample en entier à sa hauteur d'origine ; aucun réglage exposé |
| Couper ou étirer un clip audio | un clip dure son sample, comme une pose dure son pattern |
| Lignes libres façon FL (plusieurs patterns sur une ligne) | écarté à la validation : ce serait un état d'écran dans le domaine |

**IDEES.md :** aucune idée du fichier n'a servi cette semaine, et rien n'y a été mis en œuvre.

## 7 bis. La CI est passée au rouge, et la dette de la S9 est payée

`b7df698` (la playlist) et `6b7a37a` (la migration) ont été poussés avec une violation de
`check_hygiene.py` : `.reduced(0, …)` dans `PlaylistPanel.cpp`. Même un littéral d'espacement nul
est interdit dans un panneau. J'avais lancé clang-format, ruff et pytest, mais pas l'hygiène :
**exactement la dette que le bilan S9 nommait.**

- corrigé dans le commit suivant, avec `withTrimmedTop` / `withTrimmedBottom` sur le jeton ;
- `scripts/check-all.sh` existe maintenant. Il lance, dans l'ordre de la CI : clang-format,
  hygiène, manifestes, ruff, pytest, et s'arrête au premier échec. Il ne compile rien : ça reste
  le rôle des presets.

## 8. Les vérifications, faites par l'application

Tu m'as demandé de faire les vérifications moi-même. Tu as refusé deux fois que je prenne le
contrôle de l'écran, et je ne l'ai pas redemandé. C'est donc l'application qui les fait :
`--verify` rejoue la liste par les mêmes chemins qu'une personne. Les clics sont de vrais
événements souris envoyés au rack et à la playlist. PAT, SONG et « + Pattern » sont de vrais
boutons. Les Ctrl+Z passent par la vue, et la phrase est posée au vrai copilote. Chaque étape
laisse une capture de la fenêtre. Chaque étape qui change le son rend l'Edit hors ligne en WAV
et compte les attaques sur la grille du rack.

Résultat : **107 vérifications, aucune en échec**, sur trois exécutions du build final : la liste
(98 : 51 pour la playlist, 34 pour la seconde demande (§8 ter), 13 pour la barre de titre
(§8 quater)), la réouverture par un autre processus (5), un projet S8 + S9 (4). Les rapports et
six captures sont dans `docs/verification-s10/`. Les captures montrent maintenant toute la fenêtre,
barre de titre comprise.

Un premier passage de la liste a eu 5 échecs, tous au copilote, parce que l'appel au modèle a
expiré (« The handshake operation timed out »). C'était le réseau, pas le code : le second passage
les a tous passés, sans rien changer.

| # | Vérification | Mesuré |
|---|---|---|
| 1 | disposition | transport en barre ; Playlist, Channel rack, Historique et Copilote ouverts ; piano-roll fermé ; PAT allumé ; F7 ouvre le piano-roll, F7 le referme |
| 2 | « + Pattern » ne pose rien | 1 pattern, 0 pose, 1 entrée d'historique |
| 3 | le mode pattern joue sans pose | rendu de 8 s, 4 attaques aux pas 1, 5, 9, 13 |
| 4 | poser 8 fois, puis SONG | 8 poses aux mesures 1, 5 … 29 ; rendu de 64 s, 32 kicks ; la tête de lecture avance en lecture réelle |
| 5 | **la preuve** : 4 pas de hat allumés dans le rack | **64 attaques**, les 4 hats aux mêmes pas dans **chacune** des 8 poses ; 4 Ctrl+Z et on revient à 32 |
| 6 | glisser, retirer | le 4ᵉ bloc passe au temps 56, les 7 autres ne bougent pas, **une** entrée d'historique ; clic droit : bloc retiré, pattern gardé ; 2 Ctrl+Z : arrangement identique à l'octet près |
| 7 | copilote | **une** entrée marquée copilote ; pattern 2 posé une fois, après le pattern 1 ; aucun pattern créé ; **un** Ctrl+Z : état d'avant à l'octet près ; Ctrl+Y le refait |
| 8 | PAT isole le pattern 2 | rendu de 8 s, seulement ses 8 hats |
| 9 | tempo 120 → 90 | 72 s → 96 s (×4/3), chaque attaque sur son pas |
| 10 | renommer, supprimer | « Refrain » dans le sélecteur du rack ; supprimé avec ses poses ; Ctrl+Z le remet à sa place |
| 11 | fermer, rouvrir (autre processus) | même état, même profondeur d'historique (27), rouvert en PAT, même rendu (96 s, 40 attaques) |
| 12 | un projet S8 + S9 | 2 patterns, 2 poses ; rendu de 8 s, une attaque à chaque temps de 8 à 15, là où les clips commençaient |

**Ce que la vérification a trouvé.** Au premier passage, le copilote ne démarrait pas : il
cherchait `services/` à partir du **dossier courant**. Lancée ailleurs qu'à la racine du dépôt
(double-clic sur l'exécutable, par exemple), l'application lançait uv sur un dossier inexistant, et
le copilote mourait avec le code 2. Corrigé dans `2d7497d` : il cherche aussi en remontant depuis
l'exécutable. Le second passage a été lancé volontairement hors du dépôt, et le copilote a répondu.

**Ce que le copilote a répondu :** « Le pattern 1 était déjà répété huit fois (0 à 128). J'ai
ajouté le pattern 2 à la suite ». Huit poses existaient déjà, donc sa lecture est défendable. En
revanche il a d'abord écrit « à partir de la mesure 128 » alors qu'il s'agit du **temps** 128
(mesure 33). Le prompt dit maintenant que les outils parlent en temps et l'utilisateur en mesures,
avec la conversion. Au passage suivant : « à partir de la mesure 33 », juste. Il a aussi écrit
« posé huit fois (mesures 1 à 33) » : huit poses de 4 mesures vont de la mesure 1 à la 32. La
position est bonne, le compte de fin est faux d'une mesure.

**Ce qui reste hors de portée d'une vérification automatique :**
- l'écoute sur les enceintes : le rendu hors ligne en est la mesure, pas l'expérience ;
- la boîte de dialogue « Renommer » de la playlist : elle est modale, donc le renommage a été
  vérifié par le bus ;
- le glissé d'une fenêtre avec la souris : placer et replacer les fenêtres est vérifié, mais pas
  le geste de la main.

Pour tout relancer :

```bash
"build/windows-msvc/core/app/daw_app_artefacts/Debug/DAW IA.exe" --project verif.dawproj --workspace beatmaker --verify verif
```

## 8 bis. Beatmaker en fenêtres, façon FL

Tu as jugé les panneaux découpés trop brouillons. Tu as validé des fenêtres internes, et c'est fait :

- une barre fixe (le transport), puis une rangée d'onglets, un par page ;
- chaque page est une fenêtre qu'on peut déplacer, redimensionner, agrandir (bouton ou double-clic
  sur le titre), fermer, ou mettre devant d'un clic ;
- **F5** playlist, **F6** channel rack, **F7** piano-roll, **F8** plugins, **F9** pistes. Chaque
  touche ouvre sa page, la met devant, ou la ferme si elle y est déjà ;
- la place des fenêtres est gardée en fractions de l'écran dans `DAW IA.layout`, jamais dans le
  projet ;
- un panneau dans une fenêtre ne répète plus son titre sous celui de la fenêtre ;
- les trois autres workspaces gardent leurs découpes pour l'instant.

L'architecture n'est pas rouverte : c'est toujours le manifeste qui dit quelles pages existent.
Il accepte simplement une mise en page `bar` + `pages`, validée par le schéma, par
`validate_workspaces.py` et par le lecteur de manifeste.

**Encore brouillon, et je préfère le dire :** chaque panneau garde sa bande d'en-tête, désormais
vide pour la playlist et le copilote. Et un pattern de 4 mesures affiche 64 pas minuscules dans le
rack. FL ouvre un pattern sur 1 mesure ; le passer à 1 mesure par défaut serait un choix à
valider.

## 8 ter. Samples, sélection, copier-coller

Ta demande, et tes deux choix : un sample déposé sur le rack devient un **canal sampler** ; la
playlist prend des **clips audio** tout de suite.

**Le modèle.** Deux ajouts au domaine, sans migration :

| Ajout | Contenu | Commandes |
|---|---|---|
| `Track.sample` (optionnel) | `SampleRef` : empreinte des octets dans le ContentStore, nom, format, durée | `track.set_sample` |
| `AudioClip` | identifiant, piste, `SampleRef`, temps de début ; pas de longueur, il dure son sample | `audio.place`, `audio.move`, `audio.remove` |

Les octets sont **copiés dans le projet** par leur empreinte. Un projet ne dépend donc pas du
dossier d'où vient le sample : on peut vider ou déplacer le drumkit, le projet sonne pareil.
`track.remove` emporte les clips audio de la piste, et son annulation les rend avec leur rang.

**Le moteur.** Un canal sampler porte un `SamplerPlugin` à la place du synthé. Les clips audio
vivent sur une **piste compagnon** : un instrument remplace l'audio qui entre dans sa piste, et un
clip posé sur la piste du sampler aurait été muet. Le test de rendu l'a montré avant qu'un écran
le cache. Les clips audio suivent la même réconciliation par clé : déplacer un clip le repositionne
sans le recréer, et à 60 BPM il garde son objet et sa durée en secondes. En mode pattern, ils se
taisent.

**L'écran.**

| Geste | Effet |
|---|---|
| Espace | lecture / arrêt, depuis n'importe quelle page |
| page **Navigateur** (à gauche) | « + Dossier » donne accès à un drumkit ou un sample pack ; arbre ouvert à la demande ; clic droit sur un dossier racine pour le retirer ; dossiers gardés dans `DAW IA.layout` |
| déposer un sample sur une ligne du rack | ce canal devient sampler |
| déposer un sample sous les lignes du rack | nouveau canal sampler, nommé d'après le fichier ; **une** entrée d'historique |
| déposer un sample sur la playlist (depuis le navigateur ou l'Explorateur) | nouvelle piste + clip audio à la mesure sous le pointeur ; **une** entrée |
| glisser des blocs | toute la sélection bouge ; **une** entrée, quel que soit le nombre de blocs |
| Ctrl + glisser dans le vide | sélection en zone |
| Ctrl + Maj + clic | ajoute un bloc à la sélection, ou l'en retire |
| Ctrl+C, Ctrl+V | copie ; colle à la tête de lecture |
| Ctrl+B | duplique la sélection juste après elle, arrondie à la mesure ; la sélection passe aux copies, donc Ctrl+B répété enchaîne |
| Suppr | retire la sélection |

Le copilote voit les samples et les clips audio dans l'état résumé. Il peut déplacer et retirer un
clip audio. Il ne peut pas en poser un ni changer le sample d'un canal : il faudrait qu'il fournisse
des octets, ce qu'il n'a pas.

**Vérifié par l'application (34 des 85) :** Espace lance puis arrête la lecture. Le navigateur
montre un drumkit écrit pour l'occasion. Le dépôt sur le rack crée un canal « Kick 808 » en une
entrée. Ses cases allumées s'entendent au rendu, aux bons pas. Le clap déposé sur la playlist
s'entend à son temps, et le rendu va jusqu'au bout du clap. Ctrl + glisser prend 2 blocs. Ctrl +
Maj + clic en ajoute un, puis le retire. Ctrl+B ajoute 2 poses en une entrée, et un Ctrl+Z les
retire. Ctrl+C / Ctrl+V colle au temps 100. Suppr retire, et Ctrl+Z rend l'état à l'octet près.

**Le bug que tu as trouvé : un sample déposé sur la playlist ne sonnait pas.** L'application
s'ouvre en mode pattern, et le mode pattern ne joue que le pattern écouté : le clip audio était
bien posé, mais muet tant qu'on n'appuyait pas sur SONG. La vérification ne l'avait pas vu, parce
qu'elle appuyait sur SONG juste avant le dépôt. Maintenant, un dépôt sur la playlist passe en mode
chanson. La vérification dépose désormais en PAT et contrôle que SONG est pris et que le clap
s'entend. Si tu n'entends toujours rien en direct, c'est un autre bug : le rendu hors ligne, lui,
entend le clap.

**Trouvé en vérifiant :** le coller prenait l'horloge du moteur même à l'arrêt ; il prend
maintenant la position du transport quand rien ne joue. La page Navigateur était plus étroite que
sa largeur minimale, et une fenêtre se cachait sous la playlist. Elle est élargie, et la mémoire
des fenêtres se remet à zéro d'elle-même quand la disposition du manifeste change.

## 8 quater. La barre de titre

Tes trois choix : notre propre barre, un redémarrage pour ouvrir un autre projet, et le menu Fichier
proposé.

- La barre de titre Windows est remplacée par une bande aux couleurs de l'app. Elle contient le
  menu **Fichier**, le nom du projet, un mot d'état, les boutons Beatmaker / Découverte / Film /
  UGC, et **réduire, agrandir, fermer**. On la glisse pour déplacer la fenêtre, et un double-clic
  agrandit la fenêtre ou lui rend sa taille. Les bords restent redimensionnables.
- Les boutons de workspace quittent le transport : ils n'agissent pas sur la musique.
- **Fichier** :

| Entrée | Raccourci | Effet |
|---|---|---|
| Nouveau projet… | Ctrl+N | choisir un nom, puis l'app redémarre sur un projet vide |
| Ouvrir… | Ctrl+O | choisir un dossier `.dawproj`, puis l'app redémarre dessus. Un dossier qui n'est pas un projet est refusé avec un message |
| Enregistrer | Ctrl+S | écrit tout de suite ce que la sauvegarde automatique écrirait dans 30 s ; la barre dit « enregistré » |
| Enregistrer sous… | Ctrl+Maj+S | copie tout le dossier (journal, historique, samples), puis l'app redémarre sur la copie |

**Pourquoi un redémarrage.** L'application est construite autour d'un seul projet, du journal
jusqu'à l'écran. Le processus en cours enregistre et ferme le projet, écrit ses réglages, puis
lance le suivant avec `--relaunched`. Ce drapeau est la seule exception à « une seule instance » :
le nouveau processus démarre pendant que l'ancien finit de se fermer. Testé : une seconde instance
normale est refusée, une instance `--relaunched` démarre. Changer de projet sans redémarrer est un
chantier du démarrage de l'app, pour plus tard.

**Vérifié par l'application (13 des 98) :** plus de barre Windows, la barre de DAW IA affichée avec
le nom du projet, 8 boutons (Fichier, 4 workspaces, 3 boutons de fenêtre), Beatmaker allumé ; le
transport n'a plus les workspaces ; Ctrl+S affiche « enregistré » ; le double-clic agrandit, le
second rend exactement la place d'avant ; Découverte s'allume depuis la barre, puis retour au
beatmaker.

**Pas vérifié automatiquement :** Nouveau, Ouvrir et Enregistrer sous passent par la boîte de
dialogue de fichiers de Windows. Une vérification automatique ne sait pas la remplir, et le
redémarrage terminerait la vérification. Le lancement `--relaunched` a été testé à part, comme dit
plus haut. Le geste de glisser la fenêtre à la souris n'est pas vérifié non plus. L'aimantation de
Windows aux bords de l'écran est perdue, comme annoncé.

## 8 quinquies. Ce qui reste pour clore la S10

| Reste | Nature |
|---|---|
| Commit de la seconde demande, `--verify-reopen` et `--verify-legacy` sur le build final | fait |
| Prompt du copilote en mesures | fait ; il dit « mesure 33 » |
| Un sample déposé sur la playlist restait muet | corrigé (§8 ter) |
| Longueur par défaut d'un pattern | **tranché par toi : 4 mesures**, c'est déjà le cas |
| Barre de titre : fermer, réduire, agrandir, Fichier | fait (§8 quater) |
| Essayer Nouveau, Ouvrir, Enregistrer sous à la main | **à faire par toi** : c'est la boîte de dialogue Windows |
| Bandes d'en-tête vides dans les pages | cosmétique, reste |
| Écoute sur les enceintes | à faire par toi : seul point qu'aucun rendu ne remplace |

## 9. Ce que la S11 hérite

| Pièce | Usage |
|---|---|
| `ProjectProjector::stats()` | tout ajout au moteur peut prouver qu'il ne reconstruit pas plus que nécessaire |
| `transport.set_mode` | le point d'entrée pour faire écouter un pattern généré avant de le poser |
| `arrangementEndBeats`, `rank`, `label` | le vocabulaire d'arrangement du copilote |
| `SampleRef`, ContentStore par empreinte | tout audio importé ou généré entre dans le projet par le même chemin |
| Piste compagnon | le modèle pour mêler instrument et audio sur une même piste du domaine |
| Pose tronquée, défilement de la playlist, écoute dans le navigateur | reportés, sans migration |

## 10. Suivi d'avancement

| S | Visait | État |
|---|---|---|
| S1–S8 | Socle, bus, moteur, plugins, persistance, UI, domaine, copilote | Acquis |
| S9 | Channel rack, modèle de pattern | Acquis |
| S10 | La playlist, le beatmaker en fenêtres, les samples, la barre de titre | Livré ; 107 vérifications automatiques (§8) ; reste l'essai à la main de Fichier et l'écoute (§8 quinquies) |

Aucune décision d'architecture n'a été rouverte. La règle des identifiants tient : chaque
`PlacementId` est tiré par l'appelant (la playlist, le copilote avec `$new:`), jamais par une
commande.
