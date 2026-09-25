# Bilan de fin de S10 — DAW IA

**Période :** semaine 10 sur 26. Rédigé le 25 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 24 commits S10 (`295d36d` → `e74729a`),
CI verte sur le dernier.
**Volume :** 93 fichiers, +9 801 lignes, −390 (docs et rapports de vérification compris).
**Tests :** 233 cas ctest (domaine, persistance, interface), 61 cas d'engine (rendus audio), 18 cas
Python. Zéro warning.
**Vérifications par l'application :** 123, aucune en échec, sur cinq exécutions du build final
(§10).
**Registry :** 42 types de commandes, contre 33 à la fin de la S9.
**Schéma de projet :** 4, inchangé. Les samples ajoutent deux champs optionnels, écrits seulement
s'ils servent : un projet sans sample s'écrit à l'octet près comme avant.

## En bref

La S9 avait séparé le contenu de sa position. La S10 a donné un écran à la position, la playlist,
et a prouvé au rendu audio que « modifié une fois, change partout » est vrai. En cours de semaine,
tes demandes ont ajouté quatre chantiers, tous livrés :
- le beatmaker en fenêtres, façon FL ;
- les samples : navigateur, canal sampler, clips audio ;
- la sélection et le copier-coller de FL dans la playlist ;
- une barre de titre propre à l'app, avec le menu Fichier.

Tu m'as demandé de faire les vérifications moi-même, sans me donner l'écran. C'est donc
l'application qui les fait, par les mêmes chemins qu'une personne.

Il reste une seule chose, qu'aucun rendu ne remplace : ton écoute sur les enceintes. Le dépôt d'un
sample sur la playlist, lui, est déjà confirmé par ton oreille.

## 1. Livrables

| # | Livrable | État | Où |
|---|---|---|---|
| 0 | Modèle de placement et bascule pattern/chanson exposés et validés avant de coder | Livré | §2 |
| 1 | Panneau `playlist` déclaré dans `beatmaker.json` | Livré | §4 |
| 2 | Poser, déplacer, supprimer, par le bus, annulables, identifiants fournis par l'appelant | Livré | §2, §4 |
| 3 | **La preuve** : posé huit fois, modifié une fois, les huit changent | Livré, au rendu et dans l'application | §3.2, §10 point 5 |
| 4 | Le transport suit la playlist, modes pattern et chanson | Livré | §3.3 |
| 5 | Projection par identité, piège du tempo compris | Livré | §3.1 |
| 6 | Copilote : « répète le pattern 1 huit fois puis ajoute le pattern 2 » | Livré : une entrée copilote, un Ctrl+Z | §5, §10 point 7 |
| 7 | Les projets des semaines précédentes se relisent | Livré | §6 |
| 8 | *Demandé en cours de semaine :* pages en fenêtres | Livré | §7 |
| 9 | *Demandé :* barre espace, samples, glisser-déposer, sélection, Ctrl+C/V/B | Livré | §8 |
| 10 | *Demandé :* barre de titre, fermer / réduire / agrandir, ouvrir / enregistrer | Livré | §9 |

## 2. Le modèle de placement

Il n'a rien changé au modèle de la S9, et c'était l'objectif.

| Question | Réponse | Pourquoi |
|---|---|---|
| Une ligne de playlist par quoi ? | **par pattern**, déduite de l'ordre des patterns | un `Placement` ne porte pas de piste. Avec une ligne par piste, glisser un bloc déplacerait aussi le pattern sur toutes les autres pistes |
| Où sont stockées les lignes ? | nulle part | la ligne n = le n-ième pattern. Aucun état d'écran dans le domaine |
| Redimensionner une pose ? | non | la longueur appartient au pattern. Une pose tronquée serait un `lengthBeats` optionnel sur le `Placement` : un ajout sans migration, reporté |

Quatre commandes nouvelles :

| Commande | undoRecord | Coalescence |
|---|---|---|
| `placement.move` | l'ancien temps | par placement, sur le geste : un glissé = une entrée |
| `placement.remove` | le placement **et son rang** | — |
| `pattern.rename` | l'ancien nom | — |
| `pattern.remove` | le pattern, son rang, ses lignes, **chaque placement avec son rang** | — |

Les rangs sont gardés pour qu'une annulation rende *le même* arrangement, à l'octet près, pas
seulement un arrangement équivalent.

## 3. Moteur et transport

### 3.1 Réconciliation par identité

Avant, une note ajoutée effaçait puis reposait tous les clips de la piste. Maintenant, chaque clip
Tracktion porte une clé, et le projecteur ne touche que ce qui a changé :

| Changement | Effet dans l'Edit |
|---|---|
| déplacer une pose parmi huit | 1 clip repositionné, 0 inséré, 0 réécrit, **les huit mêmes objets** |
| une note ajoutée à un pattern posé 8 fois | 8 séquences réécrites, 0 insertion, 0 déplacement |
| **tempo 120 → 60** (le piège de la S7) | 4 clips replacés en secondes, 0 séquence réécrite |
| déplacer un fader | aucun clip touché |

La boucle est recalculée de la même façon : avant, un changement de tempo la laissait sur les
anciennes secondes.

### 3.2 La preuve au rendu

`ArrangementTests.cpp` rend l'Edit hors ligne, puis écoute chaque temps :

| Situation | Durée | Temps qui sonnent |
|---|---|---|
| un pattern de 4 temps, un coup, posé 8 fois | 16 s | 8 |
| une seule `note.add` **dans le pattern** | 16 s | **16** |
| un Ctrl+Z | 16 s | 8 |
| mode pattern sur un autre pattern | 2 s | 1, en boucle |

### 3.3 Mode pattern et mode chanson

- `transport.set_mode {mode, patternId}` est une commande **transitoire** : ni historique, ni
  journal. Le copilote peut basculer lui aussi.
- **Mode pattern :** l'Edit ne contient que le pattern écouté, au temps 0, en boucle sur sa
  longueur.
- **Mode chanson :** tous les placements et les clips audio.
- Boutons **PAT** et **SONG** dans le transport. L'app s'ouvre en PAT sur le premier pattern.
- **Changement de comportement dit, pas glissé :** « + Pattern » ne pose plus le pattern sur la
  timeline. Le mode pattern le joue là où il est, comme dans FL.
- Un pattern fait **4 mesures par défaut** : tu l'as confirmé.

## 4. La playlist

Page F5 du beatmaker.

| Geste | Effet |
|---|---|
| clic dans la ligne d'un pattern | `pattern.place` à la mesure (Maj : au temps) |
| glisser des blocs | toute la sélection bouge ; **une** entrée d'historique |
| clic droit | retire le bloc, ou la sélection |
| Ctrl + glisser dans le vide | sélection en zone |
| Ctrl + Maj + clic | ajoute un bloc à la sélection, ou l'en retire |
| Ctrl+C, Ctrl+V | copie ; colle à la tête de lecture |
| Ctrl+B | duplique la sélection juste après elle ; Ctrl+B répété enchaîne |
| Suppr | retire la sélection |
| clic sur l'en-tête de ligne | sélectionne le pattern ; le rack, le piano-roll et PAT suivent |
| clic droit ou double-clic sur l'en-tête | renommer, supprimer le pattern |
| clic dans la règle | déplace la tête de lecture, en mode chanson |
| déposer un sample | nouvelle piste + clip audio à la mesure ; passe en SONG (§8) |

Les poses du pattern courant sont en couleur pleine, les autres atténuées : on voit d'avance tout
ce qu'une modification du rack va changer.

## 5. Le copilote

- Les nouvelles commandes sont dans sa table. Le test croisé registry ↔ table passe dans les deux
  sens : 42 = 42.
- L'état résumé donne à chaque pattern son **rang** et son **libellé** : « le pattern 2 » désigne
  la même chose pour toi et pour lui. Il donne aussi la fin du morceau, le mode de lecture, les
  samples et les clips audio.
- Le prompt lui apprend à répéter un pattern par N `pattern.place` bout à bout, **jamais** en créant
  un nouveau pattern. Il lui apprend aussi à **parler en mesures** : les outils sont en temps, la
  conversion est donnée.
- Il peut déplacer et retirer un clip audio, pas en poser un : il faudrait qu'il fournisse des
  octets.

**Dans l'application, avec le vrai modèle** (§10 point 7) : « répète le pattern 1 huit fois puis
ajoute le pattern 2 » donne **une** entrée copilote, et un Ctrl+Z la défait entièrement. Il
répond : « Le pattern 1 était déjà posé huit fois (mesures 1 à 33). J'ai ajouté le pattern 2 à la
suite, à partir de la mesure 33. » La position est juste. Le compte de fin est faux d'une mesure :
huit poses de 4 mesures vont de la mesure 1 à la 32. Avant la correction du prompt, il disait
« mesure 128 » pour le temps 128.

## 6. Migration

**Pas de schéma 5.** La playlist n'ajoute que des types de commande. Les samples ajoutent deux
champs optionnels, absents des projets qui ne les utilisent pas.

Le test fait tourner trois processus : un enfant écrit un projet S8, un autre le migre et y ajoute
ce que le rack de la S9 écrivait, puis le parent (S10) le rouvre (22 ms pour 13 lignes), y applique
les verbes de la playlist et le ferme. Un enfant et le parent le rouvrent ensuite (28 ms pour
23 lignes) : même état, même historique, et **quatre Ctrl+Z rendent le projet de la S9 à l'octet
près**.

**Limite :** aucun binaire S8 ou S9 n'est réellement lancé. Les enfants écrivent avec les commandes
d'aujourd'hui, dont les payloads n'ont pas changé ; un test épinglé le garantit.

## 7. Beatmaker en fenêtres, façon FL

Tu as jugé les panneaux découpés trop brouillons, et validé des fenêtres internes :

- le transport en barre fixe, puis une rangée d'onglets, un par page ;
- chaque page se déplace, se redimensionne, s'agrandit, se ferme, passe devant d'un clic ;
- **F5** playlist, **F6** channel rack, **F7** piano-roll, **F8** plugins, **F9** pistes : la touche
  ouvre la page, la met devant, ou la ferme si elle y est déjà ;
- la place des fenêtres est gardée dans les réglages de la machine, jamais dans le projet. Elle se
  remet à zéro d'elle-même quand le manifeste change la disposition ;
- les trois autres workspaces gardent leurs découpes.

C'est toujours le manifeste qui dit quelles pages existent. Il accepte une mise en page `bar` +
`pages`, validée par le schéma, par `validate_workspaces.py` et par le lecteur.

## 8. Samples, et le bug que tu as trouvé

Tes deux choix : un sample déposé sur le rack devient un **canal sampler**, et la playlist prend
des **clips audio** tout de suite.

**Le domaine.**

| Ajout | Contenu | Commandes |
|---|---|---|
| `Track.sample` (optionnel) | `SampleRef` : empreinte des octets, nom, format, durée | `track.set_sample` |
| `AudioClip` | identifiant, piste, `SampleRef`, temps de début ; il dure son sample | `audio.place`, `audio.move`, `audio.remove` |

Les octets sont **copiés dans le projet**, rangés par leur empreinte. On peut vider ou déplacer le
drumkit d'origine : le projet sonne pareil. La copie « Enregistrer sous » l'a vérifié (§9).

**Le moteur.** Un canal sampler porte un `SamplerPlugin` à la place du synthé. Les clips audio
vivent sur une **piste compagnon** : un instrument remplace l'audio qui entre dans sa piste, donc un
clip posé sur la piste du sampler aurait été muet. Le test de rendu l'a montré avant l'écran. Les
clips suivent la réconciliation par clé : un déplacement les repositionne sans les recréer.

**L'écran.**
- **Espace** lance et arrête la lecture, depuis n'importe quelle page.
- La page **Navigateur**, à gauche : « + Dossier » donne accès à un drumkit ou un sample pack.
  L'arbre s'ouvre à la demande, et un clic droit retire un dossier.
- Déposer un sample **sur une ligne du rack** en fait un canal sampler. **Sous les lignes**, cela
  crée un nouveau canal. Dans les deux cas, une entrée d'historique.
- Déposer un sample **sur la playlist**, depuis le navigateur ou l'Explorateur, crée une piste et un
  clip audio en une entrée.

**Le bug que tu as trouvé : un sample déposé sur la playlist ne sonnait pas.** L'app s'ouvre en
mode pattern, qui ne joue que le pattern du rack : le clip était posé mais muet. Ma vérification
ne l'avait pas vu, parce qu'elle appuyait sur SONG juste avant le dépôt. Maintenant, un dépôt sur
la playlist passe en SONG, et la vérification dépose en PAT. **Confirmé à l'écoute par toi.**

**Trouvé en vérifiant :**
- le coller prenait l'horloge du moteur même à l'arrêt ;
- la page Navigateur était plus étroite que sa largeur minimale, et se cachait sous la playlist.

## 9. La barre de titre et le menu Fichier

Tes trois choix : notre propre barre, un redémarrage pour changer de projet, et le menu proposé.

- La barre de titre Windows est remplacée par une bande aux couleurs de l'app. Elle contient
  **Fichier**, le nom du projet, un mot d'état (« enregistré »), les workspaces et **réduire,
  agrandir, fermer**.
- On glisse la barre pour déplacer la fenêtre, et un double-clic l'agrandit ou lui rend sa taille.
  Les bords restent redimensionnables.
- Les workspaces ont quitté le transport : ils n'agissent pas sur la musique.

| Entrée | Raccourci | Effet |
|---|---|---|
| Nouveau projet… | Ctrl+N | choisir un nom ; l'app redémarre sur un projet vide |
| Ouvrir… | Ctrl+O | choisir un dossier `.dawproj` ; l'app redémarre dessus. Un autre dossier est refusé avec un message |
| Enregistrer | Ctrl+S | écrit tout de suite ce que la sauvegarde automatique écrirait dans 30 s |
| Enregistrer sous… | Ctrl+Maj+S | copie tout le projet (journal, historique, samples) ; l'app redémarre sur la copie |

**Pourquoi un redémarrage.** L'application est construite autour d'un seul projet, du journal
jusqu'à l'écran. Le processus en cours enregistre, ferme le projet, écrit ses réglages, puis lance
le suivant avec `--relaunched`, seule exception à « une seule instance ». Changer de projet sans
redémarrer est un chantier du démarrage de l'app, pour plus tard.

**Perdu, comme annoncé :** l'aimantation de Windows aux bords de l'écran.

## 10. Les vérifications, faites par l'application

Tu as refusé deux fois que je prenne l'écran, et je ne l'ai pas redemandé. `--verify` rejoue les
gestes par les mêmes chemins qu'une personne :
- de vrais événements souris et clavier sont envoyés aux panneaux, et PAT, SONG et « + Pattern »
  sont de vrais boutons ;
- la phrase est posée au vrai copilote ;
- chaque étape laisse une capture de toute la fenêtre ;
- chaque étape qui change le son rend l'Edit en WAV et compte les attaques.

| Exécution | Vérifications |
|---|---|
| `--verify` : la liste | 98 (51 playlist, 34 samples et sélection, 13 barre de titre) |
| `--verify-reopen` : fermer, rouvrir dans un autre processus | 5 |
| `--verify-legacy` : un projet S8 + S9 | 4 |
| `--verify-file` : le menu Fichier et la fenêtre, jusqu'à un vrai « Enregistrer sous » | 11 |
| `--verify-reopen` sur la copie née de cet « Enregistrer sous » | 5 |
| **Total** | **123, aucune en échec** |

Les rapports et six captures sont dans `docs/verification-s10/`.

Les points de la playlist :

| # | Vérification | Mesuré |
|---|---|---|
| 1 | disposition | transport en barre ; pages ouvertes selon le manifeste ; PAT allumé ; F7 ouvre et referme le piano-roll |
| 2 | « + Pattern » ne pose rien | 1 pattern, 0 pose, 1 entrée d'historique |
| 3 | le mode pattern joue sans pose | 8 s, 4 attaques aux pas 1, 5, 9, 13 |
| 4 | poser 8 fois, puis SONG | 8 poses ; 64 s, 32 kicks ; la tête avance en lecture réelle |
| 5 | **la preuve** : 4 hats allumés dans le rack | **64 attaques**, les 4 hats dans **chacune** des 8 poses ; 4 Ctrl+Z et on revient à 32 |
| 6 | glisser, retirer | un bloc bouge, les 7 autres non, **une** entrée ; 2 Ctrl+Z : arrangement identique à l'octet près |
| 7 | copilote | **une** entrée copilote ; pattern 2 posé une fois ; **un** Ctrl+Z : état d'avant à l'octet près ; Ctrl+Y le refait |
| 8 | PAT isole le pattern 2 | 8 s, seulement ses 8 hats |
| 9 | tempo 120 → 90 | durée ×4/3, chaque attaque sur son pas |
| 10 | renommer, supprimer | « Refrain » dans le rack ; supprimé avec ses poses ; Ctrl+Z le remet |
| 11 | fermer, rouvrir | même état, même historique (34), rouvert en PAT, même son (100,17 s, 45 attaques) |
| 12 | un projet S8 + S9 | 2 patterns, 2 poses ; une attaque à chaque temps de 8 à 15 |

Les samples et la sélection :
- Espace lance puis arrête la lecture.
- Le navigateur montre un drumkit écrit pour l'occasion.
- Le dépôt sur le rack crée « Kick 808 » en une entrée, et ses cases s'entendent aux bons pas.
- Le clap déposé en PAT fait passer en SONG et s'entend à son temps.
- La sélection : Ctrl + glisser prend 2 blocs, Ctrl + Maj + clic en ajoute puis en retire un.
- Ctrl+B ajoute 2 poses en une entrée ; Ctrl+C / Ctrl+V colle à la tête de lecture ; Suppr puis
  Ctrl+Z rend l'état à l'octet près.

La barre de titre et le menu Fichier :
- Il n'y a plus de barre Windows. La barre de DAW IA porte le nom du projet et 8 boutons.
- Ctrl+S affiche « enregistré ». Le double-clic agrandit, et le second rend la place exacte.
- Les workspaces se changent depuis la barre.
- Ouvrir refuse un dossier qui n'est pas un projet. Nouveau et Enregistrer sous refusent un nom
  pris, sans toucher au dossier.
- Glisser la barre déplace la fenêtre exactement comme le pointeur. Agrandie, elle ne se glisse
  pas.
- « Enregistrer sous » pour de vrai : le processus se ferme et un autre s'ouvre sur
  `Copie.dawproj`. Rouverte, la copie donne le même état, le même historique et le même son, clap
  compris.

**Ce que les vérifications ont trouvé et fait corriger :**
- le copilote ne démarrait pas si l'app était lancée hors du dépôt : il cherchait `services/` à
  partir du dossier courant ;
- le dépôt d'un sample restait muet en PAT (§8, et c'est toi qui l'as entendu) ;
- le coller à l'arrêt, et la page Navigateur trop étroite ;
- un refus s'affichait dans une boîte de message modale : pendant une vérification, les étapes
  continuaient dessous. Le message va maintenant au rapport ;
- JUCE déplace une fenêtre d'après le vrai pointeur. La vérification le déplace donc pendant le
  geste, puis le remet en place.

**Deux passages ratés, dits :**
- un passage de la liste a eu 5 échecs au copilote, parce que l'appel au modèle a expiré. C'était
  le réseau : le passage suivant les a tous passés sans rien changer ;
- une réouverture a échoué parce que je lui avais donné le mauvais dossier. C'était mon erreur, pas
  celle de l'app.

**Hors de portée d'un script :**
- l'écoute sur les enceintes ;
- la boîte de dialogue de fichiers de Windows. Le script appelle ce qui la suit, avec le dossier
  qu'une personne aurait choisi ;
- la boîte « Renommer » de la playlist, modale. Le renommage est vérifié par le bus.

Pour tout relancer :

```bash
"build/windows-msvc/core/app/daw_app_artefacts/Debug/DAW IA.exe" --project verif.dawproj --workspace beatmaker --verify verif
```

## 11. La CI, et la dette de la S9

`b7df698` et `6b7a37a` ont été poussés avec une violation d'hygiène : un littéral d'espacement dans
`PlaylistPanel.cpp`. J'avais lancé clang-format, ruff et pytest, mais pas l'hygiène : **exactement
la dette que le bilan S9 nommait.** Corrigé au commit suivant.

`scripts/check-all.sh` existe depuis, et il est passé avant chaque commit suivant. Il lance, dans
l'ordre de la CI, clang-format, l'hygiène, les manifestes, ruff et pytest.

Un passage de CI a été annulé, parce que j'ai poussé un commit pendant qu'il tournait. Le passage
suivant, vert, le couvre.

## 12. Ce que je n'ai pas fait

| Point | Pourquoi |
|---|---|
| Redimensionner une pose | décision validée (§2) |
| Défilement et zoom de la playlist | la timeline s'adapte à la largeur ; au-delà d'une soixantaine de mesures, les blocs deviennent étroits |
| Écouter un sample dans le navigateur | il faut le déposer pour l'entendre |
| Réglages du sampler (note de base, enveloppe) | le canal joue le sample entier, à sa hauteur |
| Couper ou étirer un clip audio | un clip dure son sample, comme une pose dure son pattern |
| Changer de projet sans redémarrer | chantier du démarrage de l'app (§9) |
| Bandes d'en-tête vides dans les pages | cosmétique |
| Lignes libres façon FL | écarté à la validation : ce serait un état d'écran dans le domaine |

**IDEES.md :** rien n'y a été mis en œuvre.

## 13. Ce que la S11 hérite

| Pièce | Usage |
|---|---|
| `ProjectProjector::stats()` | tout ajout au moteur peut prouver qu'il ne reconstruit pas plus que nécessaire |
| `transport.set_mode` | faire écouter un pattern généré avant de le poser |
| `rank`, `label`, `arrangementEndBeats`, positions en mesures | le vocabulaire d'arrangement du copilote |
| `SampleRef`, octets rangés par empreinte | tout audio importé ou généré entre dans le projet par le même chemin |
| Piste compagnon | mêler instrument et audio sous une même piste du domaine |
| `--verify`, `--verify-reopen`, `--verify-legacy`, `--verify-file` | l'app se vérifie elle-même, rendu audio compris |
| `scripts/check-all.sh` | ce que la CI vérifie hors build, en une commande |

## 14. Suivi d'avancement

| S | Visait | État |
|---|---|---|
| S1–S8 | Socle, bus, moteur, plugins, persistance, UI, domaine, copilote | Acquis |
| S9 | Channel rack, modèle de pattern | Acquis |
| S10 | Playlist ; en cours de semaine : fenêtres, samples, sélection, barre de titre | Livré ; 123 vérifications ; reste ton écoute sur les enceintes |

Aucune décision d'architecture n'a été rouverte. La règle des identifiants tient : chaque
identifiant est tiré par l'appelant (la playlist, le rack, le copilote avec `$new:`), jamais par
une commande.
