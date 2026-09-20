# Bilan de fin de S6 — DAW IA

**Période :** semaine 6 sur 26. Rédigé le 20 septembre 2026.
**Dépôt :** `samflppp/daw-ia` (privé), branche `main`, 9 commits (`d86ee99` → `c885ccd`).
**Volume :** 78 fichiers, +6 793 lignes, −151. Dont 5 fichiers de police et leurs licences.
**Tests :** 141 cas hors audio (125 de domaine, 16 de persistance), 35 cas d'engine sous label `audio`.

## 1. Livrables demandés

| # | Livrable | État | Preuve |
|---|---|---|---|
| 1 | Design system complet : `tokens.json` et le LookAndFeel | Livré | `swiss-dark` v2, 9 groupes de jetons ; `--gallery` affiche chaque jeton et chaque contrôle |
| 2 | L'écran est construit depuis le manifeste, pas codé en dur | Livré | `WorkspaceManifest` + `LayoutTree`, sans JUCE, 10 cas de test ; changer `beatmaker.json` change l'écran sans recompiler l'application |
| 3 | Les cinq panneaux du beatmaker | Livré | transport, pistes, piano-roll, chaîne de plugins, historique |
| 4 | Toute interaction passe par le bus, aucun panneau ne mute l'état | Livré | `PanelServices::state` est `const` ; les gestes continus ouvrent et ferment un geste |
| 5 | Les trois règles d'hygiène restent vérifiées | Livré, et durcies | `check_hygiene.py` refuse en plus qu'un panneau demande où il est |
| 6 | Ouvrir, éditer, fermer, rouvrir : l'écran reflète l'état rechargé | Livré, vérifié sur le binaire | 3 pistes dont une coupée, 5 notes de deux longueurs, un CLAP dans la chaîne : `12 commands replayed`, écran identique |

## 2. La décision de la semaine : ce qui a une bonne réponse est testé, le reste est regardé

La note de méthode de la semaine était juste : un écran qui compile et qui
respecte les règles peut être illisible sans qu'aucun test ne le dise. La
réponse n'a pas été d'écrire des tests d'interface, qui auraient figé des
pixels sans rien prouver, mais de **couper l'interface en deux**.

| | Ce qui a une bonne réponse | Ce qui n'en a pas |
|---|---|---|
| quoi | lecture d'un manifeste, arithmétique de découpe, liste d'historique | couleurs, espacements, lisibilité, densité |
| où | `daw_ui_workspace` : `WorkspaceManifest`, `LayoutTree`, `HistoryLog` — **sans JUCE** | `daw_ui` : jetons, LookAndFeel, panneaux |
| comment c'est tenu | 18 cas dans le preset rapide | en regardant, en critiquant, en refaisant |

Conséquence pratique : les trois fichiers les plus faciles à casser en silence
— celui qui refuse un manifeste mal formé, celui qui pose les rectangles, celui
qui dit où va une entrée d'historique — tournent dans la suite de domaine, sans
fenêtre, sans message thread, en moins d'une seconde.

## 3. Ce que regarder a trouvé, et qu'aucun test n'aurait dit

C'est la section que la note de méthode réclamait. Chaque ligne a été trouvée en
pilotant le binaire, pas en le compilant.

| Défaut | Ce qui l'a montré | Correction |
|---|---|---|
| Le bouton lecture s'allumait sur le silence | il lisait `ProjectState.transport.playing`, pas l'horloge | il lit l'horloge, seule source de ce que l'oreille entend |
| `font.size.small` n'existe pas dans les jetons | deux phrases d'état vide dessinées en taille 0, donc invisibles | `font.size.caption` ; un jeton absent reste silencieux, c'est une dette |
| Le Viewport vide peignait son fond par-dessus la phrase qui explique pourquoi la chaîne est vide | la chaîne vide était une zone noire muette | le Viewport disparaît tant qu'il n'y a rien à faire défiler |
| `+ Plugin` restait désactivé pour toujours | son état n'était mis à jour que dans `rebuild()`, et sélectionner une piste sans plugin ne reconstruit rien | l'état des contrôles est rafraîchi sur tout changement, pas seulement sur reconstruction |
| L'en-tête de la chaîne gardait « aucune piste » après sélection | même cause : pas de reconstruction, donc pas de repeint | repeint explicite sur changement de sélection |
| Le menu d'insertion listait 300 plugins à plat sur trois colonnes | illisible à l'œil, correct au compilateur | un sous-menu par format, trié par nom |
| Le compte d'un geste fusionné (`x6`) était dessiné à moitié hors du panneau | marge droite absente | même marge que le reste de la ligne |
| `« 1 notes »`, et `PIANO-ROL` tronqué | deux captures d'écran | pluriel, et largeur d'en-tête |
| Pas de molette dans le piano-roll | 34 rangées visibles sur 128, et la touche PageUp demande le focus clavier | la molette bouge d'un demi-ton, les touches d'une octave |

La dernière ligne contredit un commentaire que j'avais écrit au J7 (« la molette,
un trackpad l'envoie par accident »). L'usage réel a tranché contre lui : je le
signale plutôt que de le laisser passer en silence.

## 4. Le manifeste construit l'écran

`WorkspaceManifest` refuse plutôt que de deviner : un nœud à la fois panneau et
découpe, un `schemaVersion` inconnu, une découpe à moins de deux enfants, une
disposition qui place un panneau non déclaré, un panneau déclaré jamais placé.
Un manifeste refusé est dit au démarrage dans le journal, pas découvert par
l'utilisateur qui demande cet écran.

Les quatre manifestes existent depuis la S1 et sont tous chargés ; trois
affichent des panneaux de remplacement nommés, parce qu'un écran qui refuse de
s'ouvrir n'apprend rien et qu'un trou sans explication apprend pire.

`LayoutTree` accumule l'arrondi depuis l'origine de la surface : les parts
couvrent la surface exactement, et une surface trop petite rend des rectangles
vides, jamais négatifs.

## 5. Les panneaux, par ce qu'ils ne font pas

| Panneau | Ce qu'il ne sait pas | Ce qu'il demande à la place |
|---|---|---|
| Transport | où il est, si le projet joue | l'horloge (`TransportClock`), jamais `ProjectState` |
| Pistes | modifier un volume | `SetTrackVolume` dans un geste ouvert au `onDragStart` |
| Piano-roll | posséder une note | il relit l'état à chaque peinture ; chaque édition part en commande |
| Chaîne | ce que cette machine a comme plugins, comment ouvrir une fenêtre | `PluginHost`, implémenté côté application (`PluginRack`) |
| Historique | lire les piles d'undo du bus | il écoute les reçus et tient sa propre liste |

Aucun panneau ne reçoit sa position : `PanelServices` ne contient ni rectangle
ni voisin. La règle 2 n'est pas une convention ici, c'est la forme de la
structure — un panneau qui voudrait savoir où il est devrait qu'on lui passe
quelque chose qui n'y est pas.

## 6. Mute de piste et bypass de chaîne, demandés ensemble

Deux fonctions constamment confondues, séparées dans le domaine :

- `track.set_muted` coupe la piste entière, clips et instrument compris. Ce
  n'est pas un volume à −100 dB : le fader reste où l'utilisateur l'a laissé.
  La projection appelle le mute de Tracktion, pas un gain.
- `plugin.set_bypassed` contourne un plugin ; la chaîne laisse encore passer
  l'instrument. Le bouton `B` de la liste de pistes contourne toute la chaîne,
  c'est-à-dire émet une commande par instance **dans un seul geste** : un clic,
  un undo. Le domaine n'a pas d'interrupteur de chaîne, et en inventer un aurait
  posé une seconde vérité à côté des instances.

Un projet écrit avant l'arrivée du champ se relit : `muted` absent vaut « pas
coupée ».

## 7. Le piano-roll : quatre gestes, quatre commandes

| Geste | Commande | Fusion |
|---|---|---|
| clic sur une case vide | `clip.create_midi` puis `note.add` (deux décisions, deux entrées) | non |
| glisser une note | `note.move` | par note, une entrée par glissé |
| glisser son bord droit | `note.resize` | par note, une entrée par étirement |
| clic droit, ou Suppr | `note.remove` | non |

`note.move` et `note.resize` sont séparées pour la même raison que le mute et le
bypass : une commande qui ferait les deux rendrait à l'undo une note que
l'utilisateur n'a jamais eue. Elles ne fusionnent ni entre elles ni entre deux
notes : deux notes déplacées dans un même geste restent deux entrées, sinon
annuler l'une déplacerait l'autre.

Le glissé n'émet une commande **que lorsque le pas quantifié change**, pas à
chaque pixel : soixante images de souris ne font ni soixante commandes ni
soixante projections.

## 8. L'historique, et ce qu'il ne réinvente pas

Le bus ne donne aucune lecture de ses piles, et c'est voulu : une interface qui
pourrait les parcourir finirait par tenir des commandes qu'elle ne doit pas
toucher. L'historique reçoit donc ce que tout observateur reçoit — un reçu par
opération — et tient sa liste. C'est un miroir, jamais une seconde vérité :
le perdre coûterait un affichage, pas une édition.

Trois propriétés, toutes tenues par des tests dans le preset rapide :

- une commande transitoire (le transport) ne laisse **aucune** entrée — sinon le
  panneau afficherait une ligne qu'aucun undo ne peut atteindre ;
- un geste fusionné est **une** ligne qui dit combien de commandes elle porte ;
- exécuter après un undo **coupe la branche de redo**, ici comme dans le bus.

Chaque entrée porte son auteur (`user`, `copilot`, `generator`), et un undo ne
le réécrit pas : le copilote qui annule une édition de l'utilisateur n'en
devient pas l'auteur. Aucune des deux couleurs d'IA n'a encore de code derrière
elle ; l'affichage les attend depuis aujourd'hui.

Cliquer une entrée y ramène le projet **un undo ou un redo à la fois**. Il n'y a
pas de « saut d'état » sur le bus et il ne faut pas en ajouter un : chaque pas
du chemin est une opération dont les observateurs et le journal sont informés.

Le journal est rejoué **avant** que l'écran existe, donc un projet rouvert
montre son historique au lieu d'une liste vide au-dessus d'une pile d'undo
pleine.

## 9. Hygiène : la règle 2 avait un trou

`check_hygiene.py` interdisait de placer un composant hors d'un hôte de
disposition. Un panneau pouvait encore lire la géométrie de son parent ou de
l'écran, et en déduire sa place — la règle était contournable sans la violer.
Deux changements :

- placer un composant est autorisé aux hôtes de disposition **et aux panneaux**,
  qui arrangent les enfants qu'ils possèdent ;
- `getParentWidth`, `getScreenBounds`, `getTopLevelComponent` et leurs voisins
  sont refusés partout sauf dans un hôte de disposition.

Coût réel : deux corrections dans le code écrit cette semaine, dont un
`area.reduced(0.0f, hauteur * 0.15f)` dans un icône, remplacé par des jetons
`metric.icon.*`.

## 10. Périmètre

Respecté. Pas de branches d'arrangement, pas de services Python, pas d'IA, pas
d'automation, pas de ramasse-miettes. Les trois autres workspaces sont chargés
et pas finis, ce qui était le périmètre exact.

Une chose demandée a été faite plus tôt que prévu, à la demande : le mute de
piste, avant le J5, parce qu'il n'est pas une fonction d'interface mais un champ
d'état.

## 11. Dette

| Dette | État |
|---|---|
| Tempo non modifiable par commande | Inchangée. Le transport l'affiche, rien ne le change. C'est la dette dont le prix monte |
| Un jeton absent rend 0 en silence | **Nouvelle.** Deux textes invisibles cette semaine. Un `jassert` en debug sur un chemin inconnu coûterait trois lignes |
| Pas de renommage ni de suppression de piste dans l'interface | `track.remove` existe comme commande, aucun contrôle ne l'appelle |
| Pas de vu-mètres | Demande une source de niveau, c'est-à-dire une interface comme `TransportClock` |
| Le piano-roll montre un clip par piste | Le premier. Un deuxième clip existe dans l'état et pas à l'écran |
| Vélocité non éditable | Elle se lit (la teinte de la note), elle ne se change pas |
| Écriture synchrone à chaque commande | Inchangée depuis la S5, toujours mesurée acceptable |
| Le portage Linux | Hors périmètre MVP. Le job CI Linux reste vert en compilation, rien n'y est validé au runtime |

## 12. Suivi d'avancement

### 12.1 Les six semaines écoulées

| S | Visait | Livré | État aujourd'hui |
|---|---|---|---|
| S1 | Socle : outillage, structure, CI | CI verte deux plateformes, règles d'hygiène vérifiées | Acquis |
| S2 | Command Bus | payload / undoRecord séparés, ULID, registry, coalescing, rejeu | Acquis |
| S3 | Le bus pilote un vrai moteur audio | Projection par réconciliation, son audible | Acquis |
| S4 | Hébergement VST3 et CLAP | Scan persisté, 5 commandes plugin, fenêtre d'édition | Acquis |
| S5 | Persistance et provenance | Journal SQLite, dossier de projet, migrations, enveloppe v2 | Acquis |
| S6 | Une interface qui se montre | Un workspace fini, cinq panneaux, tout par le bus | — |

Aucune décision d'architecture n'a été rouverte depuis la S1.

### 12.2 Le jalon du 9 novembre

| Ce qui est demandé | État | Depuis |
|---|---|---|
| Un binaire Windows | Atteint | S1 |
| qui joue de l'audio | Atteint, mesuré en RMS | S3 |
| qui charge un plugin | Atteint, VST3 et CLAP | S4 |
| et accepte des commandes annulables | Atteint | S2 |
| **et qui se montre** | Atteint | S6 |

Le cinquième membre n'était pas dans le jalon ; il en conditionnait la
démonstration. Une session complète — créer des pistes, dessiner et étirer des
notes, insérer un plugin, ouvrir son éditeur, couper une piste, annuler, fermer,
rouvrir — se fait maintenant à la souris, sans `--demo` et sans ligne de
commande.

### 12.3 Ce qui n'existe toujours pas

| Manquant | Conséquence aujourd'hui |
|---|---|
| Services Python JSON-RPC | Le bus n'est pilotable que depuis le processus |
| IA | Aucune ligne, par périmètre. Le bus, la provenance et l'historique l'attendent |
| Branches d'arrangement | Le journal les rend possibles, rien ne les écrit |
| Automation, tempo variable | Le tempo est un scalaire qu'aucune commande ne change |
| Les trois autres workspaces | Chargés, affichés en panneaux de remplacement nommés |

### 12.4 Les deux semaines avant le jalon

Proposition, à arbitrer :

| S | Candidat | Pourquoi maintenant | Pourquoi pas |
|---|---|---|---|
| S7 | **Tempo en séquence** | Coûte presque rien aujourd'hui, coûte cher après les clips audio | Ne se voit pas à l'écran |
| S7 | Services Python JSON-RPC | Socle du MCP, donc du copilote | Rouvre la règle de thread du bus |
| S7 | Branches d'arrangement | Le différenciateur produit, et le schéma a été conçu pour | Deux semaines, pas une |
| S8 | Gel, répétition de la démonstration, bilan de jalon | La démonstration se répète, elle ne s'improvise pas | — |

Mon avis, une ligne : **tempo en S7**, parce que c'est la seule dette dont le
prix augmente avec le temps, et la semaine est courte ; la S8 sert au gel et à
la démonstration.

### 12.5 Les risques

| Risque | Coût s'il se réalise | Ce qui le tient aujourd'hui |
|---|---|---|
| Le tempo scalaire rencontre un clip audio | Élevé : change des payloads déjà écrits sur des projets réels | Rien. C'est la dette à payer tôt |
| La démonstration tombe sur un plugin qui plante | Moyen : la fenêtre meurt avec le plugin | Le scan blackliste ce qui a tué le scanner ; la fenêtre se ferme avant le retrait du plugin |
| Un jeton renommé passe inaperçu | Faible mais silencieux : un texte invisible | Rien aujourd'hui, voir §11 |
| La règle de thread du bus face à une socket Python | Moyen : la règle est vérifiée en release | `rebindToCurrentThread()` existe pour la passation délibérée |
| Le portage Linux | Nul avant le MVP | Décision de périmètre |

### 12.6 Ce qui a tenu cette semaine

1. **Exposer la direction avant d'écrire.** Trois directions visuelles proposées,
   une validée, puis les jetons — pas l'inverse.
2. **Regarder, critiquer, refaire.** Neuf défauts du §3 n'existent que pour
   l'œil : aucun test ne les aurait vus, et les corriger a pris moins de temps
   que de les écrire ici.
3. **Dire quand un choix contredit l'acquis.** La molette du piano-roll
   contredit un commentaire écrit trois jours plus tôt ; c'est dit, pas effacé.
